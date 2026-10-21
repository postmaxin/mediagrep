#include "doctest.h"
#include "parser.h"

using namespace mediagrep;

namespace {
std::string p(std::string_view text) { return toString(*parse(text)); }

// Returns the byte offset of the ParseError, or -1 if parsing succeeded.
long errorOffset(std::string_view text) {
    try {
        parse(text);
        return -1;
    } catch (const ParseError& e) {
        return static_cast<long>(e.offset);
    }
}
}  // namespace

TEST_CASE("parses a simple equality comparison") {
    CHECK(p("Title = foo") == "(= Title \"foo\")");
}

TEST_CASE("parses every comparison operator") {
    CHECK(p("a != 1") == "(!= a \"1\")");
    CHECK(p("a ~ x") == "(~ a \"x\")");
    CHECK(p("a !~ x") == "(!~ a \"x\")");
    CHECK(p("a < 1") == "(< a \"1\")");
    CHECK(p("a <= 1") == "(<= a \"1\")");
    CHECK(p("a > 1") == "(> a \"1\")");
    CHECK(p("a >= 1") == "(>= a \"1\")");
}

TEST_CASE("operators do not need surrounding whitespace") {
    CHECK(p("Video/Height<1000") == "(< Video/Height \"1000\")");
    CHECK(p("a>=1") == "(>= a \"1\")");
}

TEST_CASE("field may be scoped to a track kind") {
    CHECK(p("Video/Height < 1000") == "(< Video/Height \"1000\")");
}

TEST_CASE("track kind names are case-insensitive and normalized") {
    CHECK(p("audio/Language = ja") == "(= Audio/Language \"ja\")");
    CHECK(p("GENERAL/Title = x") == "(= General/Title \"x\")");
}

TEST_CASE("field may be scoped to one 1-based track index") {
    CHECK(p("Audio[2]/Language = ja") == "(= Audio[2]/Language \"ja\")");
}

TEST_CASE("a leading segment that is not a track kind is part of the field name") {
    // MediaInfo parameter names themselves contain slashes: Duration/String,
    // Format/Info. Only a recognized track kind is split off.
    CHECK(p("Duration/String = x") == "(= Duration/String \"x\")");
    CHECK(p("Video/Format/Info = x") == "(= Video/Format/Info \"x\")");
}

TEST_CASE("values may be double- or single-quoted and contain spaces") {
    CHECK(p("Title = \"Interesting Engineering\"") ==
          "(= Title \"Interesting Engineering\")");
    CHECK(p("Title = 'Interesting Engineering'") ==
          "(= Title \"Interesting Engineering\")");
}

TEST_CASE("backslash escapes the quote character inside a string") {
    CHECK(p("Title = \"say \\\"hi\\\"\"") == "(= Title \"say \\\"hi\\\"\")");
    CHECK(p("Title = 'it\\'s'") == "(= Title \"it's\")");
    CHECK(p("Title = \"a\\\"b\"") == "(= Title \"a\\\"b\")");
}

// toString() shows a backslash in a value as two.
TEST_CASE("any other backslash in a string is kept, so regex escapes survive") {
    CHECK(p("Title ~ \"^\\d+$\"") == "(~ Title \"^\\\\d+$\")");
    CHECK(p("Title ~ 'a\\.b'") == "(~ Title \"a\\\\.b\")");
    CHECK(p("Title ~ \"\\(x\\)\\s\\w\"") == "(~ Title \"\\\\(x\\\\)\\\\s\\\\w\")");
    // \\ is one backslash, so a doubled-backslash habit still works.
    CHECK(p("Title ~ \"\\\\d\"") == "(~ Title \"\\\\d\")");
    CHECK(p("Title = \"\\\\\"") == "(= Title \"\\\\\")");
    // The other quote character is not special inside a string, so \' stays.
    CHECK(p("Title ~ \"it\\'s\"") == "(~ Title \"it\\\\'s\")");
    // A backslash before the closing quote of an unterminated string is
    // not swallowed silently.
    CHECK(errorOffset("Title ~ \"abc\\\"") == 8);
    // A lone trailing backslash is kept and the string is unterminated.
    CHECK(errorOffset("Title ~ \"abc\\") == 8);
}

TEST_CASE("a track index too large for a number is a parse error") {
    CHECK(errorOffset("Audio[18446744073709551617]/Lang = 1") == 6);
    CHECK(errorOffset("Audio[99999999999999999999999]/Lang = 1") == 6);
    CHECK(p("Audio[12]/Lang = 1") == "(= Audio[12]/Lang \"1\")");
}

TEST_CASE("unquoted values keep units and signs") {
    CHECK(p("Duration > 1h30m") == "(> Duration \"1h30m\")");
    CHECK(p("Delay < -5") == "(< Delay \"-5\")");
    CHECK(p("FileSize >= 1.5G") == "(>= FileSize \"1.5G\")");
}

TEST_CASE("a bare quoted string is a pattern over all fields") {
    CHECK(p("\"engineering\"") == "(pattern \"engineering\")");
    CHECK(p("'foo(bar|baz)'") == "(pattern \"foo(bar|baz)\")");
}

TEST_CASE("a bare field is an exists test") {
    CHECK(p("Audio/Language") == "(exists Audio/Language)");
    CHECK(p("Title") == "(exists Title)");
}

TEST_CASE("and binds tighter than or") {
    CHECK(p("a = 1 or b = 2 and c = 3") ==
          "(or (= a \"1\") (and (= b \"2\") (= c \"3\")))");
}

TEST_CASE("chains of the same operator flatten") {
    CHECK(p("a = 1 and b = 2 and c = 3") ==
          "(and (= a \"1\") (= b \"2\") (= c \"3\"))");
    CHECK(p("a = 1 or b = 2 or c = 3") ==
          "(or (= a \"1\") (= b \"2\") (= c \"3\"))");
}

TEST_CASE("not applies to the following atom only") {
    CHECK(p("not a = 1 and b = 2") == "(and (not (= a \"1\")) (= b \"2\"))");
    CHECK(p("not not a = 1") == "(not (not (= a \"1\")))");
}

TEST_CASE("parentheses group") {
    CHECK(p("(a = 1 or b = 2) and c = 3") ==
          "(and (or (= a \"1\") (= b \"2\")) (= c \"3\"))");
    CHECK(p("not (a = 1 or b)") == "(not (or (= a \"1\") (exists b)))");
}

TEST_CASE("keywords are case-insensitive") {
    CHECK(p("a = 1 AND b = 2 Or NOT c") ==
          "(or (and (= a \"1\") (= b \"2\")) (not (exists c)))");
}

TEST_CASE("mixed pattern and comparison") {
    CHECK(p("(Duration > 1h or Format = Matroska) and \"60 minutes\"") ==
          "(and (or (> Duration \"1h\") (= Format \"Matroska\")) (pattern \"60 minutes\"))");
}

TEST_CASE("surrounding whitespace is ignored") {
    CHECK(p("   a = 1   ") == "(= a \"1\")");
}

TEST_CASE("parse errors carry the byte offset of the problem") {
    CHECK(errorOffset("") == 0);
    CHECK(errorOffset("   ") == 3);
    CHECK(errorOffset("Title =") == 7);        // missing value
    CHECK(errorOffset("(a = 1") == 6);         // missing ')'
    CHECK(errorOffset("a = 1 b = 2") == 6);    // trailing garbage
    CHECK(errorOffset("a = 1 and") == 9);      // dangling operator
    CHECK(errorOffset("Audio[x]/Lang = 1") == 6);
    CHECK(errorOffset("Audio[0]/Lang = 1") == 6);
    CHECK(errorOffset("Title = \"unterminated") == 8);
    CHECK(errorOffset("= 1") == 0);
    CHECK(errorOffset(")") == 0);
}

TEST_CASE("parse errors carry a message") {
    try {
        parse("Title =");
        FAIL("expected ParseError");
    } catch (const ParseError& e) {
        CHECK(std::string(e.what()).find("value") != std::string::npos);
    }
}

TEST_CASE("File is a track kind and @names are ordinary field names") {
    CHECK(p("File/Size > 1G") == "(> File/Size \"1G\")");
    CHECK(p("file/name ~ talk") == "(~ File/name \"talk\")");
    CHECK(p("@when = 2024") == "(= @when \"2024\")");
    CHECK(p("@who") == "(exists @who)");
}
