#include "eval.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <unordered_map>
#include <utility>

#include "thread.h"
#include "units.h"

namespace mediagrep {

std::string MatchedField::location() const {
    std::string out(trackKindName(track));
    if (multi) out += "[" + std::to_string(index + 1) + "]";
    return out + "/" + name;
}

namespace {

constexpr std::array<TrackKind, 9> kAllKinds{
    TrackKind::General, TrackKind::Video, TrackKind::Audio, TrackKind::Text, TrackKind::Image,
    TrackKind::Menu,    TrackKind::Other, TrackKind::File,  TrackKind::Path,
};

// Tracks mediagrep derives from the path, which bare patterns skip.
bool isPathTrack(TrackKind kind) { return kind == TrackKind::File || kind == TrackKind::Path; }

std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Applies an ordering operator to a three-way comparison result.
bool applyOrder(Op op, int cmp) {
    switch (op) {
        case Op::Eq: return cmp == 0;
        case Op::Ne: return cmp != 0;
        case Op::Lt: return cmp < 0;
        case Op::Le: return cmp <= 0;
        case Op::Gt: return cmp > 0;
        case Op::Ge: return cmp >= 0;
        case Op::Match:
        case Op::NotMatch: break;
    }
    return false;
}

int threeWay(double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); }

// General fields MediaInfoLib derives from the path rather than the file's
// contents. Bare patterns skip them so a search for a channel name is not
// satisfied by the folder it was downloaded into. Explicit comparisons on
// these fields still work.
bool isPathDerived(std::string_view name) {
    for (std::string_view n : {"CompleteName", "FolderName", "FileName", "FileNameExtension",
                               "FileExtension", "CompleteName_Last", "FolderName_Last",
                               "FileName_Last", "FileNameExtension_Last", "FileExtension_Last"})
        if (iequals(n, name)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Aliases: "@when", "@who", "@name" expand to an OR over concrete fields.
// They live behind the sigil so they can never collide with a library name.
// ---------------------------------------------------------------------------

struct Alias {
    std::string_view name;
    std::vector<std::string_view> members;  // "Track/Field" or "Field"
};

const std::vector<Alias>& aliases() {
    static const std::vector<Alias> table{
        {"when", {"File/Modified", "File/Created", "Encoded_Date", "Tagged_Date", "Recorded_Date",
                  "Released_Date", "Original/Released_Date", "Mastered_Date", "DATE", "Path/When"}},
        {"who", {"Performer", "Artist", "Publisher", "Director", "Composer", "Author", "Channel"}},
        {"name", {"File/Name", "Title", "Movie", "Track", "Album"}},
    };
    return table;
}

bool isAlias(const FieldRef& ref) { return !ref.name.empty() && ref.name[0] == '@'; }

const Alias* findAlias(std::string_view name) {
    if (!name.empty() && name[0] == '@') name.remove_prefix(1);
    for (const Alias& a : aliases())
        if (iequals(a.name, name)) return &a;
    return nullptr;
}

// "File/Modified" -> scoped ref; "Original/Released_Date" -> unscoped, since
// Original is not a track kind. Same rule as the parser.
FieldRef memberRef(std::string_view spec) {
    FieldRef ref;
    const std::size_t slash = spec.find('/');
    if (slash != std::string_view::npos) {
        if (const auto kind = trackKindFromName(spec.substr(0, slash))) {
            ref.track = *kind;
            ref.name = std::string(spec.substr(slash + 1));
            return ref;
        }
    }
    ref.name = std::string(spec);
    return ref;
}

std::vector<FieldRef> expand(const FieldRef& ref) {
    if (!isAlias(ref)) return {ref};
    std::vector<FieldRef> out;
    for (std::string_view m : findAlias(ref.name)->members) out.push_back(memberRef(m));
    return out;
}

struct Result {
    bool ok = false;
    std::vector<MatchedField> fields;
};

}  // namespace

struct Evaluator::Impl {
    const Expr* root;
    EvalOptions options;
    std::unordered_map<const Expr*, std::regex> regexes;

    Impl(const Expr& expr, EvalOptions opts) : root(&expr), options(opts) { compile(expr); }

    void compile(const Expr& e) {
        for (const auto& c : e.children) compile(*c);
        if ((e.kind == Expr::Kind::Compare || e.kind == Expr::Kind::Exists) && isAlias(e.field)) {
            if (e.field.track != TrackKind::Any)
                throw EvalError("alias '" + e.field.name + "' cannot be scoped to a track");
            if (!findAlias(e.field.name))
                throw EvalError("unknown alias '" + e.field.name + "'");
        }
        const bool needsRegex =
            e.kind == Expr::Kind::Pattern ||
            (e.kind == Expr::Kind::Compare && (e.op == Op::Match || e.op == Op::NotMatch));
        if (!needsRegex) return;
        auto flags = std::regex::ECMAScript;
        if (options.ignoreCase) flags |= std::regex::icase;
        try {
            regexes.emplace(&e, std::regex(e.value, flags));
        } catch (const std::regex_error& err) {
            throw EvalError("invalid regex '" + e.value + "': " + err.what());
        }
    }

    // Calls fn(kind, index, multi) for every track a FieldRef refers to.
    template <class Fn>
    void forEachInScope(const FieldRef& ref, const MetadataSource& src, Fn fn) const {
        auto visitKind = [&](TrackKind kind) {
            const std::size_t n = src.trackCount(kind);
            const bool multi = n > 1;
            if (ref.index) {
                const std::size_t i = *ref.index - 1;
                if (i < n) fn(kind, i, multi);
                return;
            }
            for (std::size_t i = 0; i < n; ++i) fn(kind, i, multi);
        };
        if (ref.track == TrackKind::Any) {
            for (TrackKind k : kAllKinds) visitKind(k);
        } else {
            visitKind(ref.track);
        }
    }

    static void warnOnce(std::vector<std::string>& warnings, std::string message) {
        if (std::find(warnings.begin(), warnings.end(), message) == warnings.end())
            warnings.push_back(std::move(message));
    }

    // Searches `value` with e's regex. Returns nullopt, with a warning, when
    // the value is too long for this thread's stack (libstdc++ recurses per
    // character) or the engine gives up (MSVC's error_stack).
    std::optional<bool> search(const Expr& e, const std::string& value,
                               std::vector<std::string>& warnings) const {
        const std::size_t limit = threadRegexLimit();
        if (value.size() > limit) {
            warnOnce(warnings, "regex '" + e.value + "' was not applied to values longer than " +
                                   std::to_string(limit) + " characters");
            return std::nullopt;
        }
        try {
            return std::regex_search(value, regexes.at(&e));
        } catch (const std::regex_error& err) {
            warnOnce(warnings, "regex '" + e.value + "' gave up on a long value (" + err.what() + ")");
            return std::nullopt;
        }
    }

    bool compareValue(const Expr& e, const FieldValue& fv, std::vector<std::string>& warnings) const {
        if (fv.type == ValueType::Binary) return false;

        if (e.op == Op::Match || e.op == Op::NotMatch) {
            const std::optional<bool> found = search(e, fv.value, warnings);
            if (!found) return false;  // no answer satisfies neither ~ nor !~
            return e.op == Op::Match ? *found : !*found;
        }

        // Numeric when declared so, or when the library attached a unit of
        // measure to a numeric-looking value (it declares FileSize as text).
        const bool numeric = fv.type == ValueType::Integer || fv.type == ValueType::Float ||
                             (!fv.measure.empty() && parseNumber(fv.value, "").has_value());
        if (numeric) {
            const std::optional<double> operand = parseNumber(e.value, fv.measure);
            if (!operand) {
                warnOnce(warnings, "field " + fv.name + ": operand '" + e.value + "' is not a number");
                return false;
            }
            const double value = std::strtod(fv.value.c_str(), nullptr);
            return applyOrder(e.op, threeWay(value, *operand));
        }

        // Text and Date. Date-shaped on both sides: compare at the operand's
        // granularity. Otherwise plain string comparison.
        if (const auto dateOperand = normalizeDateOperand(e.value)) {
            if (const auto dateValue = normalizeDateValue(fv.value)) {
                const std::string prefix = dateValue->substr(0, dateOperand->size());
                return applyOrder(e.op, prefix.compare(*dateOperand));
            }
        }
        if (options.ignoreCase)
            return applyOrder(e.op, lowered(fv.value).compare(lowered(e.value)));
        return applyOrder(e.op, fv.value.compare(e.value));
    }

    // Exists or Compare over one concrete field reference.
    void evalField(const Expr& e, const FieldRef& ref, const MetadataSource& src, Result& r,
                   std::vector<std::string>& warnings) const {
        forEachInScope(ref, src, [&](TrackKind kind, std::size_t i, bool multi) {
            const auto fv = src.lookup(kind, i, ref.name);
            if (!fv) return;
            const bool hit = e.kind == Expr::Kind::Exists ? !fv->value.empty()
                                                           : compareValue(e, *fv, warnings);
            if (!hit) return;
            r.ok = true;
            r.fields.push_back({kind, i, multi, fv->name, fv->value});
        });
    }

    Result eval(const Expr& e, const MetadataSource& src, std::vector<std::string>& warnings) const {
        Result r;
        switch (e.kind) {
            case Expr::Kind::And:
                for (const auto& c : e.children) {
                    Result sub = eval(*c, src, warnings);
                    if (!sub.ok) return Result{};
                    append(r.fields, sub.fields);
                }
                r.ok = true;
                return r;

            case Expr::Kind::Or:
                for (const auto& c : e.children) {
                    Result sub = eval(*c, src, warnings);
                    if (!sub.ok) continue;
                    r.ok = true;
                    append(r.fields, sub.fields);
                }
                return r;

            case Expr::Kind::Not:
                r.ok = !eval(*e.children.at(0), src, warnings).ok;
                return r;

            case Expr::Kind::Pattern: {
                for (TrackKind kind : kAllKinds) {
                    if (isPathTrack(kind)) continue;
                    const std::size_t n = src.trackCount(kind);
                    for (std::size_t i = 0; i < n; ++i) {
                        src.forEachField(kind, i, [&](const FieldValue& fv) {
                            if (fv.type == ValueType::Binary) return;
                            if (kind == TrackKind::General && isPathDerived(fv.name)) return;
                            const std::optional<bool> found = search(e, fv.value, warnings);
                            if (!found || !*found) return;
                            r.ok = true;
                            r.fields.push_back({kind, i, n > 1, fv.name, fv.value});
                        });
                    }
                }
                return r;
            }

            case Expr::Kind::Exists:
            case Expr::Kind::Compare:
                for (const FieldRef& ref : expand(e.field)) evalField(e, ref, src, r, warnings);
                return r;
        }
        return r;
    }

    static void append(std::vector<MatchedField>& into, std::vector<MatchedField>& from) {
        for (auto& f : from) into.push_back(std::move(f));
    }
};

Evaluator::Evaluator(const Expr& expr, EvalOptions options)
    : impl_(std::make_unique<Impl>(expr, options)) {}
Evaluator::~Evaluator() = default;
Evaluator::Evaluator(Evaluator&&) noexcept = default;
Evaluator& Evaluator::operator=(Evaluator&&) noexcept = default;

Match Evaluator::evaluate(const MetadataSource& source) const {
    Match m;
    Result r = impl_->eval(*impl_->root, source, m.warnings);
    m.matched = r.ok;
    for (auto& f : r.fields) {
        const bool dup = std::any_of(m.fields.begin(), m.fields.end(), [&](const MatchedField& g) {
            return g.track == f.track && g.index == f.index && g.name == f.name;
        });
        if (!dup) m.fields.push_back(std::move(f));
    }
    return m;
}

}  // namespace mediagrep
