#include <string>
#include <vector>

#include "doctest.h"
#include "term.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace mediagrep;

namespace {

// All chunks joined, to compare with the expected UTF-16.
std::u16string joined(const std::vector<std::u16string>& chunks) {
    std::u16string all;
    for (const std::u16string& c : chunks) all += c;
    return all;
}

}  // namespace

TEST_CASE("toUtf16Chunks converts ASCII, BMP and astral text") {
    CHECK(joined(term::toUtf16Chunks("abc", 100)) == u"abc");
    CHECK(joined(term::toUtf16Chunks("caf\xC3\xA9 \xE2\x82\xAC", 100)) == u"café €");
    // U+1F600 is the surrogate pair D83D DE00.
    CHECK(joined(term::toUtf16Chunks("\xF0\x9F\x98\x80", 100)) == u"\xD83D\xDE00");
    CHECK(term::toUtf16Chunks("", 100).empty());
}

TEST_CASE("toUtf16Chunks turns malformed bytes into U+FFFD and keeps going") {
    CHECK(joined(term::toUtf16Chunks("a\xFF" "b", 100)) == u"a�" "b");
    CHECK(joined(term::toUtf16Chunks("a\xC3", 100)) == u"a�");           // truncated
    CHECK(joined(term::toUtf16Chunks("\xC0\x80", 100)) == u"��");   // overlong NUL
    CHECK(joined(term::toUtf16Chunks("\xED\xA0\x80", 100)) ==                  // a surrogate
          u"���");
    CHECK(joined(term::toUtf16Chunks("\xF4\x90\x80\x80", 100)) ==              // > U+10FFFF
          u"����");
    CHECK(joined(term::toUtf16Chunks("\xC3" "x", 100)) == u"�" "x");     // bad continuation
}

TEST_CASE("toUtf16Chunks never exceeds the limit or splits a surrogate pair") {
    std::string text;
    std::u16string expected;
    for (int i = 0; i < 50; ++i) {
        text += "a\xF0\x9F\x98\x80";  // one unit, then a pair
        expected += u"a\xD83D\xDE00";
    }
    for (std::size_t limit : {2u, 3u, 4u, 5u, 7u, 16u}) {
        const std::vector<std::u16string> chunks = term::toUtf16Chunks(text, limit);
        CHECK(joined(chunks) == expected);
        for (const std::u16string& c : chunks) {
            CHECK(c.size() <= limit);
            CHECK(!c.empty());
            // A chunk starts at a code point and ends at one.
            CHECK(!(c.front() >= 0xDC00 && c.front() <= 0xDFFF));
            CHECK(!(c.back() >= 0xD800 && c.back() <= 0xDBFF));
        }
    }
}

TEST_CASE("toUtf16Chunks treats a limit below 2 as 2, so a pair always fits") {
    const std::vector<std::u16string> chunks = term::toUtf16Chunks("\xF0\x9F\x98\x80z", 1);
    REQUIRE(chunks.size() == 2);
    CHECK(chunks[0] == u"\xD83D\xDE00");
    CHECK(chunks[1] == u"z");
}

#ifdef _WIN32

TEST_CASE("isConsoleHandle is false for a pipe, a file and NUL, and for no handle") {
    CHECK_FALSE(term::isConsoleHandle(nullptr));
    CHECK_FALSE(term::isConsoleHandle(INVALID_HANDLE_VALUE));

    HANDLE read = nullptr;
    HANDLE write = nullptr;
    REQUIRE(CreatePipe(&read, &write, nullptr, 0));
    CHECK_FALSE(term::isConsoleHandle(read));
    CHECK_FALSE(term::isConsoleHandle(write));
    CloseHandle(read);
    CloseHandle(write);

    // NUL is a character device, which the CRT's _isatty calls a terminal.
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(nul != INVALID_HANDLE_VALUE);
    CHECK_FALSE(term::isConsoleHandle(nul));
    CloseHandle(nul);
}

namespace {

// Gives the process a console if it has none (CI runs it with pipes), points
// `which` (STD_OUTPUT_HANDLE or STD_ERROR_HANDLE) at the console screen buffer,
// runs `write`, and reads the text back from the buffer. This checks the real
// WriteConsoleW path without a person looking at a window.
std::wstring writeToConsoleAndRead(DWORD which, bool (*write)(std::string_view),
                                   std::string_view text, std::size_t cells, bool& wasConsole) {
    AllocConsole();  // fails harmlessly when the process already has one
    HANDLE screen = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    REQUIRE(screen != INVALID_HANDLE_VALUE);
    SetConsoleCursorPosition(screen, COORD{0, 0});

    HANDLE saved = GetStdHandle(which);
    SetStdHandle(which, screen);
    wasConsole = which == STD_OUTPUT_HANDLE ? term::stdoutIsTerminal() : term::stderrIsTerminal();
    const bool ok = write(text);
    SetStdHandle(which, saved);
    CHECK(ok);

    std::wstring read(cells, L' ');
    DWORD got = 0;
    REQUIRE(ReadConsoleOutputCharacterW(screen, read.data(), static_cast<DWORD>(cells),
                                        COORD{0, 0}, &got));
    read.resize(got);
    CloseHandle(screen);
    return read;
}

}  // namespace

TEST_CASE("writeStdout and writeStderr put non-ASCII text on a console, whatever its code page") {
    const std::string text = "caf\xC3\xA9 \xE2\x82\xAC \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    const std::wstring expected = L"café € Привет";
    for (const DWORD which : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        bool wasConsole = false;
        const auto write = which == STD_OUTPUT_HANDLE ? term::writeStdout : term::writeStderr;
        const std::wstring read = writeToConsoleAndRead(which, write, text, expected.size(), wasConsole);
        CHECK(wasConsole);
        CHECK(read == expected);
    }
}

TEST_CASE("a console write longer than one chunk arrives whole") {
    std::string text;
    for (int i = 0; i < 9000; ++i) text += "\xC3\xA9";  // 9000 x U+00E9
    bool wasConsole = false;
    const std::wstring read = writeToConsoleAndRead(STD_OUTPUT_HANDLE, term::writeStdout, text,
                                                    9000, wasConsole);
    CHECK(wasConsole);
    CHECK(read == std::wstring(9000, L'é'));
}

#endif
