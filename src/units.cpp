#include "units.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace mediagrep {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// A decimal number kept as digits, so "1.001" is 1001 / 10^3 and not the
// nearest binary fraction. `exact` is false when the digits do not fit.
struct Decimal {
    bool negative = false;
    std::uint64_t mantissa = 0;
    int scale = 0;  // digits after the point
    bool exact = true;
    double approx = 0;  // strtod of the same text, for when exact is false
};

constexpr int kMaxDigits = 19;  // 10^19 - 1 fits in 64 bits

// Reads a decimal number ("-12", "1.5", ".5") at the start of `s`.
// Returns the number of characters consumed, 0 if there is no number.
std::size_t readNumber(std::string_view s, Decimal& out) {
    out = Decimal();
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        out.negative = s[i] == '-';
        ++i;
    }
    std::size_t digits = 0;
    bool afterPoint = false;
    auto take = [&](char c) {
        ++digits;
        if (digits > static_cast<std::size_t>(kMaxDigits)) {
            out.exact = false;
            return;
        }
        out.mantissa = out.mantissa * 10 + static_cast<std::uint64_t>(c - '0');
        if (afterPoint) ++out.scale;
    };
    while (i < s.size() && isDigit(s[i])) take(s[i++]);
    if (i < s.size() && s[i] == '.') {
        ++i;
        afterPoint = true;
        while (i < s.size() && isDigit(s[i])) take(s[i++]);
    }
    if (digits == 0) return 0;
    out.approx = std::strtod(std::string(s.substr(0, i)).c_str(), nullptr);
    return i;
}

std::uint64_t pow10u(int n) {
    std::uint64_t r = 1;
    for (int i = 0; i < n; ++i) r *= 10;
    return r;
}

// Adds up terms of the form number * multiplier, where the multiplier is a
// whole number of base units (ms, bytes), so "1.001s" is 1001 ms rather than
// 1000.9999999999999. The sum is kept as an integer over a power of ten and
// divided once, which rounds the way strtod rounds the same decimal text, so
// it compares equal to a field value of that number. If any term has too many
// digits, or the sum would overflow, the result is the plain floating-point
// one.
class Sum {
public:
    void add(const Decimal& d, std::uint64_t multiplier) {
        approx_ += d.approx * static_cast<double>(multiplier);
        terms_.push_back({d, multiplier});
    }

    double value() const {
        constexpr std::uint64_t kLimit = std::uint64_t(1) << 62;
        int scale = 0;
        for (const Term& t : terms_) {
            if (!t.number.exact) return approx_;
            scale = std::max(scale, t.number.scale);
        }
        std::int64_t total = 0;
        for (const Term& t : terms_) {
            const std::uint64_t scaled = pow10u(scale - t.number.scale);
            std::uint64_t magnitude = t.number.mantissa;
            if (!mulWithin(magnitude, scaled, kLimit) || !mulWithin(magnitude, t.multiplier, kLimit))
                return approx_;
            const std::int64_t term = t.number.negative ? -static_cast<std::int64_t>(magnitude)
                                                         : static_cast<std::int64_t>(magnitude);
            total += term;  // |total| and |term| are at most 2^62, so no overflow
            if (total > static_cast<std::int64_t>(kLimit) ||
                total < -static_cast<std::int64_t>(kLimit))
                return approx_;
        }
        // volatile: on x87 (32-bit x86) the quotient would otherwise stay in
        // an 80-bit register, and not equal the double strtod gives.
        volatile double result = static_cast<double>(total) / static_cast<double>(pow10u(scale));
        return result;
    }

private:
    struct Term {
        Decimal number;
        std::uint64_t multiplier;
    };

    static bool mulWithin(std::uint64_t& a, std::uint64_t b, std::uint64_t limit) {
        if (a != 0 && b > limit / a) return false;
        a *= b;
        return true;
    }

    std::vector<Term> terms_;
    double approx_ = 0;
};

std::string lowerAlpha(std::string_view s, std::size_t& pos) {
    std::string out;
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos])))
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(s[pos++])));
    return out;
}

std::optional<double> parseDuration(std::string_view text) {
    std::size_t pos = 0;
    Sum total;
    bool sawComponent = false;
    while (pos < text.size()) {
        Decimal n;
        const std::size_t len = readNumber(text.substr(pos), n);
        if (len == 0) return std::nullopt;
        pos += len;
        const std::string unit = lowerAlpha(text, pos);
        if (unit.empty()) {
            // A bare number is only allowed on its own, and means seconds.
            if (sawComponent || pos != text.size()) return std::nullopt;
            total.add(n, 1000);
            return total.value();
        }
        std::uint64_t mult = 0;
        if (unit == "h") mult = 3600000;
        else if (unit == "m") mult = 60000;
        else if (unit == "s") mult = 1000;
        else if (unit == "ms") mult = 1;
        else return std::nullopt;
        total.add(n, mult);
        sawComponent = true;
    }
    return sawComponent ? std::optional<double>(total.value()) : std::nullopt;
}

std::optional<double> parseScaled(std::string_view text) {
    Decimal n;
    std::size_t pos = readNumber(text, n);
    if (pos == 0) return std::nullopt;
    std::string suffix = lowerAlpha(text, pos);
    if (pos != text.size()) return std::nullopt;
    std::uint64_t mult = 1;
    if (!suffix.empty()) {
        if (suffix.size() == 2 && suffix[1] == 'b') suffix.pop_back();  // "GB", "Kb"
        if (suffix.size() != 1) return std::nullopt;
        switch (suffix[0]) {
            case 'k': mult = 1000ull; break;
            case 'm': mult = 1000000ull; break;
            case 'g': mult = 1000000000ull; break;
            case 't': mult = 1000000000000ull; break;
            default: return std::nullopt;
        }
    }
    Sum total;
    total.add(n, mult);
    return total.value();
}

std::optional<double> parsePlain(std::string_view text) {
    Decimal n;
    const std::size_t len = readNumber(text, n);
    if (len == 0 || len != text.size()) return std::nullopt;
    return n.approx;
}

// Matches a run of `pattern` where 'D' means digit and any other
// character matches itself. Returns true and advances `pos` on success.
bool matchShape(std::string_view s, std::size_t& pos, std::string_view pattern) {
    if (s.size() - pos < pattern.size()) return false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = s[pos + i];
        if (pattern[i] == 'D' ? !isDigit(c) : c != pattern[i]) return false;
    }
    pos += pattern.size();
    return true;
}

// What may follow a date (and time) in MediaInfo's output: with seconds,
// fractional seconds; after a time, "Z" or a numeric offset (+HH, +HH:MM,
// +HHMM); then an optional " UTC" suffix. Trailing spaces are ignored.
bool matchDateTail(std::string_view tail, bool hasTime, bool hasSeconds) {
    std::size_t i = 0;
    if (hasSeconds && i < tail.size() && tail[i] == '.') {
        std::size_t digits = 0;
        ++i;
        while (i < tail.size() && isDigit(tail[i])) { ++i; ++digits; }
        if (digits == 0) return false;
    }
    if (hasTime && i < tail.size() && tail[i] == 'Z') {
        ++i;
    } else if (hasTime && i < tail.size() && (tail[i] == '+' || tail[i] == '-')) {
        ++i;
        std::size_t digits = 0;
        while (i < tail.size() && isDigit(tail[i]) && digits < 2) { ++i; ++digits; }
        if (digits != 2) return false;
        if (i < tail.size() && tail[i] == ':') ++i;
        if (i < tail.size()) {
            digits = 0;
            while (i < tail.size() && isDigit(tail[i]) && digits < 2) { ++i; ++digits; }
            if (digits != 2) return false;
        }
    }
    std::size_t spaces = 0;
    while (i < tail.size() && tail[i] == ' ') { ++i; ++spaces; }
    if (spaces > 0 && tail.substr(i, 3) == "UTC") {
        i += 3;
        while (i < tail.size() && tail[i] == ' ') ++i;
    }
    return i == tail.size();
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

}  // namespace

std::optional<double> parseNumber(std::string_view text, std::string_view measure) {
    if (measure == " ms") return parseDuration(text);
    if (measure == " byte" || measure == " bps") return parseScaled(text);
    return parsePlain(text);
}

std::optional<std::string> normalizeDateValue(std::string_view value) {
    if (startsWithNoCase(value, "UTC")) value.remove_prefix(3);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);

    // Compact YYYYMMDD, as yt-dlp writes DATE. Only with a plausible month
    // and day, so an eight-digit number is not mistaken for a date.
    if (value.size() == 8 && std::all_of(value.begin(), value.end(), isDigit)) {
        const int month = (value[4] - '0') * 10 + (value[5] - '0');
        const int day = (value[6] - '0') * 10 + (value[7] - '0');
        if (month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
        return std::string(value.substr(0, 4)) + "-" + std::string(value.substr(4, 2)) + "-" +
               std::string(value.substr(6, 2)) + " 00:00:00";
    }

    std::size_t pos = 0;
    if (!matchShape(value, pos, "DDDD-DD-DD")) return std::nullopt;
    std::string out(value.substr(0, 10));

    std::string time = "00:00:00";
    bool seconds = false;
    bool hasTime = false;
    if (pos < value.size() && (value[pos] == ' ' || value[pos] == 'T')) {
        const std::size_t timeAt = pos + 1;
        std::size_t p = timeAt;
        if (matchShape(value, p, "DD:DD")) {
            std::size_t q = p;
            if (matchShape(value, q, ":DD")) {
                p = q;
                seconds = true;
            }
            time = std::string(value.substr(timeAt, p - timeAt));
            if (time.size() == 5) time += ":00";
            pos = p;
            hasTime = true;
        }
    }
    // Only what MediaInfo writes may follow, or "2024-03-01.mkv" and
    // "2024-03-01 Interview" would count as dates and equal the bare date.
    if (!matchDateTail(value.substr(pos), hasTime, seconds)) return std::nullopt;
    return out + " " + time;
}

std::string formatUtcTime(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d UTC", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::optional<std::string> normalizeDateOperand(std::string_view text) {
    std::size_t pos = 0;
    if (!matchShape(text, pos, "DDDD")) return std::nullopt;
    std::string out(text.substr(0, 4));
    if (pos == text.size()) return out;

    if (!matchShape(text, pos, "-DD")) return std::nullopt;
    out += text.substr(pos - 3, 3);
    if (pos == text.size()) return out;

    if (!matchShape(text, pos, "-DD")) return std::nullopt;
    out += text.substr(pos - 3, 3);
    if (pos == text.size()) return out;

    if (text[pos] != ' ' && text[pos] != 'T') return std::nullopt;
    ++pos;
    if (!matchShape(text, pos, "DD:DD")) return std::nullopt;
    out += " ";
    out += text.substr(pos - 5, 5);
    if (pos == text.size()) return out;

    if (!matchShape(text, pos, ":DD")) return std::nullopt;
    out += text.substr(pos - 3, 3);
    if (pos != text.size()) return std::nullopt;
    return out;
}

}  // namespace mediagrep
