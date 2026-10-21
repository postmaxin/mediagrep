#include "doctest.h"
#include "units.h"

using namespace mediagrep;

TEST_CASE("duration operands: bare numbers are seconds, result is milliseconds") {
    CHECK(parseNumber("5400", " ms") == 5400000.0);
    CHECK(parseNumber("0.5", " ms") == 500.0);
}

TEST_CASE("duration operands accept h, m, s, ms suffixes, combinable") {
    CHECK(parseNumber("1h30m", " ms") == 5400000.0);
    CHECK(parseNumber("90m", " ms") == 5400000.0);
    CHECK(parseNumber("45s", " ms") == 45000.0);
    CHECK(parseNumber("500ms", " ms") == 500.0);
    CHECK(parseNumber("1.5h", " ms") == 5400000.0);
    CHECK(parseNumber("1h30m15s", " ms") == 5415000.0);
    CHECK(parseNumber("1H", " ms") == 3600000.0);
}

TEST_CASE("duration operands reject malformed text") {
    CHECK_FALSE(parseNumber("abc", " ms"));
    CHECK_FALSE(parseNumber("", " ms"));
    CHECK_FALSE(parseNumber("1x", " ms"));
    CHECK_FALSE(parseNumber("h", " ms"));
    CHECK_FALSE(parseNumber("1h30", " ms"));  // trailing bare number is ambiguous
}

TEST_CASE("size and bitrate operands accept decimal K/M/G/T suffixes") {
    CHECK(parseNumber("1.5G", " byte") == 1.5e9);
    CHECK(parseNumber("700M", " byte") == 7e8);
    CHECK(parseNumber("128K", " bps") == 128000.0);
    CHECK(parseNumber("128k", " bps") == 128000.0);
    CHECK(parseNumber("2T", " byte") == 2e12);
    CHECK(parseNumber("1.5GB", " byte") == 1.5e9);
    CHECK(parseNumber("1000", " byte") == 1000.0);
}

TEST_CASE("size operands reject duration suffixes and garbage") {
    CHECK_FALSE(parseNumber("1h", " byte"));
    CHECK_FALSE(parseNumber("1GX", " byte"));
    CHECK_FALSE(parseNumber("G", " byte"));
}

TEST_CASE("other measures take plain numbers only") {
    CHECK(parseNumber("1920", " pixel") == 1920.0);
    CHECK(parseNumber("29.970", " fps") == doctest::Approx(29.97));
    CHECK(parseNumber("-5", "") == -5.0);
    CHECK_FALSE(parseNumber("1h", " pixel"));
    CHECK_FALSE(parseNumber("1K", ""));
    CHECK_FALSE(parseNumber("", ""));
}

TEST_CASE("decimal operands are exact, not off by a rounding error") {
    // 1.001 * 1000.0 is 1000.9999999999999 in binary floating point.
    CHECK(parseNumber("1.001s", " ms") == 1001.0);
    CHECK(parseNumber("1.001", " ms") == 1001.0);
    CHECK(parseNumber("0.001s", " ms") == 1.0);
    CHECK(parseNumber("2.675s", " ms") == 2675.0);
    CHECK(parseNumber("1.1m", " ms") == 66000.0);
    CHECK(parseNumber("0.1h", " ms") == 360000.0);
    CHECK(parseNumber("1h2.003s", " ms") == 3602003.0);
    CHECK(parseNumber("1.001K", " byte") == 1001.0);
    CHECK(parseNumber("4.35M", " bps") == 4350000.0);
    CHECK(parseNumber("0.001T", " byte") == 1e9);
}

TEST_CASE("decimal operands that are not whole in the base unit stay fractional") {
    CHECK(parseNumber("0.5ms", " ms") == 0.5);
    CHECK(parseNumber("1.0005s", " ms") == 1000.5);
    CHECK(parseNumber("-1.001s", " ms") == -1001.0);
    CHECK(parseNumber("1h-30m", " ms") == 1800000.0);
}

TEST_CASE("operands too long for exact arithmetic still parse approximately") {
    const auto n = parseNumber("12345678901234567890123.5s", " ms");
    REQUIRE(n.has_value());
    CHECK(*n == doctest::Approx(1.2345678901234568e25));
    const auto tiny = parseNumber("0.00000000000000000000001s", " ms");
    REQUIRE(tiny.has_value());
    CHECK(*tiny == doctest::Approx(1e-20));
}

TEST_CASE("date values normalize to YYYY-MM-DD HH:MM:SS") {
    CHECK(normalizeDateValue("UTC 2024-03-01 12:00:00") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01 12:00:00") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01") == "2024-03-01 00:00:00");
    CHECK(normalizeDateValue("2024-03-01T12:00:00Z") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01 12:00:00.123") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("UTC 2024-03-01") == "2024-03-01 00:00:00");
}

TEST_CASE("MediaInfo's trailing forms are accepted: UTC suffix, offsets, fractions") {
    CHECK(normalizeDateValue("2024-03-01 12:00:00 UTC") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("UTC 2024-03-01 12:00:00.5") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01T12:34:56+01:00") == "2024-03-01 12:34:56");
    CHECK(normalizeDateValue("2024-03-01T12:34:56-0500") == "2024-03-01 12:34:56");
    CHECK(normalizeDateValue("2024-03-01T12:34:56.789Z") == "2024-03-01 12:34:56");
    CHECK(normalizeDateValue("2024-03-01 12:34") == "2024-03-01 12:34:00");
    CHECK(normalizeDateValue("2024-03-01 12:34:56 ") == "2024-03-01 12:34:56");
}

TEST_CASE("a date followed by other text is not a date") {
    CHECK_FALSE(normalizeDateValue("2024-03-01.mkv"));
    CHECK_FALSE(normalizeDateValue("2024-03-01 Interview"));
    CHECK_FALSE(normalizeDateValue("2024-03-01 12:00:00 Interview"));
    CHECK_FALSE(normalizeDateValue("2024-03-01 12:00:00.mkv"));
    CHECK_FALSE(normalizeDateValue("2024-03-01T"));
    CHECK_FALSE(normalizeDateValue("2024-03-01Z"));
    CHECK_FALSE(normalizeDateValue("2024-03-01 12:00:00 UTCX"));
    CHECK_FALSE(normalizeDateValue("2024-03-01 12:00:00+"));
    CHECK_FALSE(normalizeDateValue("2024-03-011"));
    CHECK_FALSE(normalizeDateValue("UTC 2024-03-01 and more"));
}

TEST_CASE("values without a dashed date are not dates") {
    CHECK_FALSE(normalizeDateValue("Matroska"));
    CHECK_FALSE(normalizeDateValue("2024"));
    CHECK_FALSE(normalizeDateValue("1080"));
    CHECK_FALSE(normalizeDateValue(""));
    CHECK_FALSE(normalizeDateValue("2024-3-1"));
}

TEST_CASE("date operands keep their granularity") {
    CHECK(normalizeDateOperand("2024") == "2024");
    CHECK(normalizeDateOperand("2024-03") == "2024-03");
    CHECK(normalizeDateOperand("2024-03-01") == "2024-03-01");
    CHECK(normalizeDateOperand("2024-03-01 12:00") == "2024-03-01 12:00");
    CHECK(normalizeDateOperand("2024-03-01T12:00") == "2024-03-01 12:00");
    CHECK(normalizeDateOperand("2024-03-01 12:00:00") == "2024-03-01 12:00:00");
}

TEST_CASE("date operands reject non-dates") {
    CHECK_FALSE(normalizeDateOperand("20240301"));
    CHECK_FALSE(normalizeDateOperand("2024-3-1"));
    CHECK_FALSE(normalizeDateOperand("foo"));
    CHECK_FALSE(normalizeDateOperand("2024-"));
    CHECK_FALSE(normalizeDateOperand("2024-03-01 12"));
}

TEST_CASE("formatUtcTime renders epoch seconds as a UTC date") {
    CHECK(formatUtcTime(0) == "1970-01-01 00:00:00 UTC");
    CHECK(formatUtcTime(1709294400) == "2024-03-01 12:00:00 UTC");
}

TEST_CASE("compact YYYYMMDD values are dates when month and day are plausible") {
    CHECK(normalizeDateValue("20260121") == "2026-01-21 00:00:00");
    CHECK_FALSE(normalizeDateValue("12345678"));  // month 34
    CHECK_FALSE(normalizeDateValue("20261301"));  // month 13
    CHECK_FALSE(normalizeDateValue("20260132"));  // day 32
    CHECK_FALSE(normalizeDateValue("2026012"));   // seven digits
}

TEST_CASE("file-time values with a trailing UTC normalize") {
    CHECK(normalizeDateValue("2026-01-22 03:14:05 UTC") == "2026-01-22 03:14:05");
}
