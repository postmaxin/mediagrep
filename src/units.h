// Parsing of user-supplied operands: numbers with units, and dates.
#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace mediagrep {

// Parses a numeric operand in the units implied by a MediaInfo "measure"
// string (the Info_Measure of the field being compared):
//   " ms"            "1h30m", "90m", "45s", "500ms"; a bare number is seconds.
//                    Result is milliseconds.
//   " byte", " bps"  "1.5G", "700M", "128K", "2T", optionally followed by
//                    "B"; decimal multiples. A bare number is the raw unit.
//   anything else    a plain decimal number; unit suffixes are an error.
// Returns nullopt if the text is not a valid number for that measure.
std::optional<double> parseNumber(std::string_view text, std::string_view measure);

// Normalizes a date-shaped metadata value to "YYYY-MM-DD HH:MM:SS".
// Accepts MediaInfo's forms: "UTC 2024-03-01 12:00:00", "2024-03-01",
// "2024-03-01T12:00:00Z", fractional seconds, a numeric offset, a trailing
// " UTC". A value must contain the dashed date to count; a bare number is
// not a date, and neither is a date followed by anything else
// ("2024-03-01.mkv", "2024-03-01 Interview"), so `=` on a name or title
// compares text, not dates.
std::optional<std::string> normalizeDateValue(std::string_view value);

// Normalizes a user-supplied date operand, keeping its granularity:
// "2024", "2024-03", "2024-03-01", "2024-03-01 12:00",
// "2024-03-01 12:00:00" (a "T" separator is accepted). The result is a
// prefix of the normalized value form, so comparisons are prefix
// comparisons at the operand's granularity.
std::optional<std::string> normalizeDateOperand(std::string_view text);

// "YYYY-MM-DD HH:MM:SS UTC" for a time_t, the form used for File/ times.
std::string formatUtcTime(std::time_t t);

}  // namespace mediagrep
