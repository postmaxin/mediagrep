// Expression tree for mediagrep queries.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mediagrep {

// General..Other are MediaInfoLib's stream kinds. File is mediagrep's own
// track, filled from stat(): Name, Directory, Extension, Size, Modified, ...
enum class TrackKind { Any, General, Video, Audio, Text, Image, Menu, Other, File };

// "General", "Video", ...; "Any" has no name.
std::string_view trackKindName(TrackKind kind);
// Case-insensitive lookup. Returns nullopt for unknown names.
std::optional<TrackKind> trackKindFromName(std::string_view name);

enum class Op { Eq, Ne, Match, NotMatch, Lt, Le, Gt, Ge };

std::string_view opName(Op op);

// A reference to a metadata field, optionally scoped to a track kind and
// a 1-based track index: "Title", "Audio/Language", "Audio[2]/Language".
struct FieldRef {
    TrackKind track = TrackKind::Any;
    std::optional<std::size_t> index;  // 1-based
    std::string name;

    std::string toString() const;
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum class Kind { And, Or, Not, Pattern, Exists, Compare };

    Kind kind;
    std::vector<ExprPtr> children;  // And/Or: two or more; Not: one
    FieldRef field;                 // Exists, Compare
    Op op = Op::Eq;                 // Compare
    std::string value;              // Pattern: regex source; Compare: operand
};

// S-expression rendering, used by tests and error messages:
//   (or (and (< Video/Height 1000) (not (= Audio/Language ja))) (pattern "x"))
std::string toString(const Expr& expr);

}  // namespace mediagrep
