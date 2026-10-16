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

TEST_CASE("date values normalize to YYYY-MM-DD HH:MM:SS") {
    CHECK(normalizeDateValue("UTC 2024-03-01 12:00:00") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01 12:00:00") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01") == "2024-03-01 00:00:00");
    CHECK(normalizeDateValue("2024-03-01T12:00:00Z") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("2024-03-01 12:00:00.123") == "2024-03-01 12:00:00");
    CHECK(normalizeDateValue("UTC 2024-03-01") == "2024-03-01 00:00:00");
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
