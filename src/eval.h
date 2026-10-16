// Evaluates a parsed expression against the metadata of one file.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "expr.h"

namespace mediagrep {

// MediaInfoLib's InfoOption_TypeOfValue.
enum class ValueType { Text, Integer, Float, Date, Binary };

struct FieldValue {
    std::string name;      // canonical spelling, as the library reports it
    std::string value;     // raw value (ReadByHuman=0)
    ValueType type = ValueType::Text;
    std::string measure;   // MediaInfo Info_Measure, e.g. " ms", " byte", " bps"
};

// The seam between the evaluator and MediaInfoLib. Track indexes are
// 0-based here; the user-facing syntax is 1-based.
struct MetadataSource {
    virtual ~MetadataSource() = default;
    virtual std::size_t trackCount(TrackKind kind) const = 0;
    // Case-insensitive field lookup. nullopt if the field is absent.
    virtual std::optional<FieldValue> lookup(TrackKind kind, std::size_t index,
                                             std::string_view name) const = 0;
    virtual void forEachField(TrackKind kind, std::size_t index,
                              const std::function<void(const FieldValue&)>& fn) const = 0;
};

struct MatchedField {
    TrackKind track;
    std::size_t index;  // 0-based
    bool multi;         // the file has more than one track of this kind
    std::string name;
    std::string value;

    // "Audio[2]/Language" when multi, otherwise "Audio/Language".
    std::string location() const;
};

struct Match {
    bool matched = false;
    std::vector<MatchedField> fields;    // what satisfied comparisons
    std::vector<std::string> warnings;   // e.g. non-numeric operand on a numeric field
};

struct EvalOptions {
    bool ignoreCase = false;
};

struct EvalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Evaluator {
public:
    // Compiles the regexes in `expr`. Throws EvalError on a bad regex.
    // `expr` must outlive the Evaluator.
    Evaluator(const Expr& expr, EvalOptions options);
    ~Evaluator();
    Evaluator(Evaluator&&) noexcept;
    Evaluator& operator=(Evaluator&&) noexcept;

    Match evaluate(const MetadataSource& source) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mediagrep
