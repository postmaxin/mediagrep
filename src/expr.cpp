#include "expr.h"

#include <array>
#include <cctype>

namespace mediagrep {

namespace {

struct TrackName {
    TrackKind kind;
    std::string_view name;
};

constexpr std::array<TrackName, 7> kTrackNames{{
    {TrackKind::General, "General"},
    {TrackKind::Video, "Video"},
    {TrackKind::Audio, "Audio"},
    {TrackKind::Text, "Text"},
    {TrackKind::Image, "Image"},
    {TrackKind::Menu, "Menu"},
    {TrackKind::Other, "Other"},
}};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

std::string_view trackKindName(TrackKind kind) {
    for (const auto& t : kTrackNames)
        if (t.kind == kind) return t.name;
    return "";
}

std::optional<TrackKind> trackKindFromName(std::string_view name) {
    for (const auto& t : kTrackNames)
        if (iequals(t.name, name)) return t.kind;
    return std::nullopt;
}

std::string_view opName(Op op) {
    switch (op) {
        case Op::Eq: return "=";
        case Op::Ne: return "!=";
        case Op::Match: return "~";
        case Op::NotMatch: return "!~";
        case Op::Lt: return "<";
        case Op::Le: return "<=";
        case Op::Gt: return ">";
        case Op::Ge: return ">=";
    }
    return "?";
}

std::string FieldRef::toString() const {
    std::string out;
    if (track != TrackKind::Any) {
        out += trackKindName(track);
        if (index) out += "[" + std::to_string(*index) + "]";
        out += "/";
    }
    out += name;
    return out;
}

namespace {
std::string quoted(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
}  // namespace

std::string toString(const Expr& expr) {
    using Kind = Expr::Kind;
    switch (expr.kind) {
        case Kind::And:
        case Kind::Or: {
            std::string out = expr.kind == Kind::And ? "(and" : "(or";
            for (const auto& c : expr.children) out += " " + toString(*c);
            return out + ")";
        }
        case Kind::Not:
            return "(not " + toString(*expr.children.at(0)) + ")";
        case Kind::Pattern:
            return "(pattern " + quoted(expr.value) + ")";
        case Kind::Exists:
            return "(exists " + expr.field.toString() + ")";
        case Kind::Compare:
            return "(" + std::string(opName(expr.op)) + " " + expr.field.toString() +
                   " " + quoted(expr.value) + ")";
    }
    return "(?)";
}

}  // namespace mediagrep
