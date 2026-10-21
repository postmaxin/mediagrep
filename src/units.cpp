#include "units.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace mediagrep {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads a decimal number ("-12", "1.5", ".5") at the start of `s`.
// Returns the number of characters consumed, 0 if there is no number.
std::size_t readNumber(std::string_view s, double& out) {
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    std::size_t digits = 0;
    while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
    }
    if (digits == 0) return 0;
    out = std::strtod(std::string(s.substr(0, i)).c_str(), nullptr);
    return i;
}

std::string lowerAlpha(std::string_view s, std::size_t& pos) {
    std::string out;
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos])))
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(s[pos++])));
    return out;
}

std::optional<double> parseDuration(std::string_view text) {
    std::size_t pos = 0;
    double total = 0;
    bool sawComponent = false;
    while (pos < text.size()) {
        double n = 0;
        const std::size_t len = readNumber(text.substr(pos), n);
        if (len == 0) return std::nullopt;
        pos += len;
        const std::string unit = lowerAlpha(text, pos);
        if (unit.empty()) {
            // A bare number is only allowed on its own, and means seconds.
            if (sawComponent || pos != text.size()) return std::nullopt;
            return n * 1000.0;
        }
        double mult = 0;
        if (unit == "h") mult = 3600000.0;
        else if (unit == "m") mult = 60000.0;
        else if (unit == "s") mult = 1000.0;
        else if (unit == "ms") mult = 1.0;
        else return std::nullopt;
        total += n * mult;
        sawComponent = true;
    }
    return sawComponent ? std::optional<double>(total) : std::nullopt;
}

std::optional<double> parseScaled(std::string_view text) {
    double n = 0;
    std::size_t pos = readNumber(text, n);
    if (pos == 0) return std::nullopt;
    std::string suffix = lowerAlpha(text, pos);
    if (pos != text.size()) return std::nullopt;
    if (suffix.empty()) return n;
    if (suffix.size() == 2 && suffix[1] == 'b') suffix.pop_back();  // "GB", "Kb"
    if (suffix.size() != 1) return std::nullopt;
    switch (suffix[0]) {
        case 'k': return n * 1e3;
        case 'm': return n * 1e6;
        case 'g': return n * 1e9;
        case 't': return n * 1e12;
    }
    return std::nullopt;
}

std::optional<double> parsePlain(std::string_view text) {
    double n = 0;
    const std::size_t len = readNumber(text, n);
    if (len == 0 || len != text.size()) return std::nullopt;
    return n;
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
