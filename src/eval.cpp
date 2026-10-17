#include "eval.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <unordered_map>

#include "units.h"

namespace mediagrep {

std::string MatchedField::location() const {
    std::string out(trackKindName(track));
    if (multi) out += "[" + std::to_string(index + 1) + "]";
    return out + "/" + name;
}

namespace {

constexpr std::array<TrackKind, 7> kAllKinds{
    TrackKind::General, TrackKind::Video, TrackKind::Audio, TrackKind::Text,
    TrackKind::Image,   TrackKind::Menu,  TrackKind::Other,
};

std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
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

    bool compareValue(const Expr& e, const FieldValue& fv, std::vector<std::string>& warnings) const {
        if (fv.type == ValueType::Binary) return false;

        if (e.op == Op::Match || e.op == Op::NotMatch) {
            const bool found = std::regex_search(fv.value, regexes.at(&e));
            return e.op == Op::Match ? found : !found;
        }

        // Numeric when declared so, or when the library attached a unit of
        // measure to a numeric-looking value (it declares FileSize as text).
        const bool numeric = fv.type == ValueType::Integer || fv.type == ValueType::Float ||
                             (!fv.measure.empty() && parseNumber(fv.value, "").has_value());
        if (numeric) {
            const std::optional<double> operand = parseNumber(e.value, fv.measure);
            if (!operand) {
                const std::string msg = "field " + fv.name + ": operand '" + e.value +
                                        "' is not a number";
                if (std::find(warnings.begin(), warnings.end(), msg) == warnings.end())
                    warnings.push_back(msg);
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
                const std::regex& re = regexes.at(&e);
                for (TrackKind kind : kAllKinds) {
                    const std::size_t n = src.trackCount(kind);
                    for (std::size_t i = 0; i < n; ++i) {
                        src.forEachField(kind, i, [&](const FieldValue& fv) {
                            if (fv.type == ValueType::Binary) return;
                            if (!std::regex_search(fv.value, re)) return;
                            r.ok = true;
                            r.fields.push_back({kind, i, n > 1, fv.name, fv.value});
                        });
                    }
                }
                return r;
            }

            case Expr::Kind::Exists:
                forEachInScope(e.field, src, [&](TrackKind kind, std::size_t i, bool multi) {
                    const auto fv = src.lookup(kind, i, e.field.name);
                    if (!fv || fv->value.empty()) return;
                    r.ok = true;
                    r.fields.push_back({kind, i, multi, fv->name, fv->value});
                });
                return r;

            case Expr::Kind::Compare:
                forEachInScope(e.field, src, [&](TrackKind kind, std::size_t i, bool multi) {
                    const auto fv = src.lookup(kind, i, e.field.name);
                    if (!fv || !compareValue(e, *fv, warnings)) return;
                    r.ok = true;
                    r.fields.push_back({kind, i, multi, fv->name, fv->value});
                });
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
