// In-memory MetadataSource for evaluator tests.
#pragma once

#include <cctype>
#include <map>
#include <vector>

#include "eval.h"

namespace mediagrep::test {

inline FieldValue text(std::string name, std::string value) {
    return FieldValue{std::move(name), std::move(value), ValueType::Text, ""};
}
inline FieldValue integer(std::string name, std::string value, std::string measure = "") {
    return FieldValue{std::move(name), std::move(value), ValueType::Integer, std::move(measure)};
}
inline FieldValue flt(std::string name, std::string value, std::string measure = "") {
    return FieldValue{std::move(name), std::move(value), ValueType::Float, std::move(measure)};
}
inline FieldValue binary(std::string name, std::string value) {
    return FieldValue{std::move(name), std::move(value), ValueType::Binary, ""};
}

class FakeSource : public MetadataSource {
public:
    FakeSource& add(TrackKind kind, std::vector<FieldValue> fields) {
        tracks_[kind].push_back(std::move(fields));
        return *this;
    }

    std::size_t trackCount(TrackKind kind) const override {
        auto it = tracks_.find(kind);
        return it == tracks_.end() ? 0 : it->second.size();
    }

    std::optional<FieldValue> lookup(TrackKind kind, std::size_t index,
                                     std::string_view name) const override {
        auto it = tracks_.find(kind);
        if (it == tracks_.end() || index >= it->second.size()) return std::nullopt;
        for (const auto& f : it->second[index])
            if (iequals(f.name, name)) return f;
        return std::nullopt;
    }

    void forEachField(TrackKind kind, std::size_t index,
                      const std::function<void(const FieldValue&)>& fn) const override {
        auto it = tracks_.find(kind);
        if (it == tracks_.end() || index >= it->second.size()) return;
        for (const auto& f : it->second[index]) fn(f);
    }

private:
    static bool iequals(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }

    std::map<TrackKind, std::vector<std::vector<FieldValue>>> tracks_;
};

}  // namespace mediagrep::test
