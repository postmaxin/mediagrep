#include "doctest.h"
#include "utf.h"

using namespace mediagrep;

TEST_CASE("ASCII round-trips") {
    CHECK(widen("hello") == L"hello");
    CHECK(narrow(L"hello") == "hello");
}

TEST_CASE("two- and three-byte sequences round-trip") {
    const std::string s = "h\xC3\xA9llo \xE2\x82\xAC";  // héllo €
    CHECK(narrow(widen(s)) == s);
    CHECK(widen(s).size() == 7);
}

TEST_CASE("four-byte sequences round-trip, using surrogates when wchar_t is 16-bit") {
    const std::string s = "\xF0\x9F\x8E\xAC";  // U+1F3AC clapper board
    const std::wstring w = widen(s);
    if (sizeof(wchar_t) == 2) {
        REQUIRE(w.size() == 2);
        CHECK(static_cast<unsigned>(w[0]) == 0xD83C);
        CHECK(static_cast<unsigned>(w[1]) == 0xDFAC);
    } else {
        REQUIRE(w.size() == 1);
        CHECK(static_cast<unsigned long>(w[0]) == 0x1F3AC);
    }
    CHECK(narrow(w) == s);
}

TEST_CASE("invalid UTF-8 becomes U+FFFD") {
    CHECK(widen("a\xFFz") == L"a\xFFFDz");
    CHECK(widen("a\xC3") == L"a\xFFFD");      // truncated sequence
    CHECK(widen("\xE2\x82") == L"\xFFFD");    // truncated sequence
}

TEST_CASE("empty strings") {
    CHECK(widen("").empty());
    CHECK(narrow(L"").empty());
}
