#include "term.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mediagrep {
namespace term {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// Decodes one code point at s[i]. A malformed or truncated sequence, an
// overlong form, a surrogate or a value past U+10FFFF consumes one byte and
// gives U+FFFD, so the rest of the text still decodes.
char32_t decodeOne(std::string_view s, std::size_t i, std::size_t& length) {
    const auto b = static_cast<unsigned char>(s[i]);
    length = 1;
    if (b < 0x80) return b;
    std::size_t len = 0;
    char32_t cp = 0;
    char32_t min = 0;
    if (b >= 0xC2 && b < 0xE0) { len = 2; cp = b & 0x1F; min = 0x80; }
    else if (b >= 0xE0 && b < 0xF0) { len = 3; cp = b & 0x0F; min = 0x800; }
    else if (b >= 0xF0 && b < 0xF5) { len = 4; cp = b & 0x07; min = 0x10000; }
    else return kReplacement;
    if (i + len > s.size()) return kReplacement;
    for (std::size_t k = 1; k < len; ++k) {
        const auto c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return kReplacement;
        cp = (cp << 6) | (c & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return kReplacement;
    length = len;
    return cp;
}

bool writeStdio(std::string_view text, std::FILE* stream) {
    if (text.empty()) return true;
    return std::fwrite(text.data(), 1, text.size(), stream) == text.size() && !std::ferror(stream);
}

}  // namespace

std::vector<std::u16string> toUtf16Chunks(std::string_view utf8, std::size_t maxUnits) {
    maxUnits = std::max<std::size_t>(maxUnits, 2);
    std::vector<std::u16string> chunks;
    std::u16string current;
    std::size_t i = 0;
    while (i < utf8.size()) {
        std::size_t length = 1;
        const char32_t cp = decodeOne(utf8, i, length);
        i += length;
        const std::size_t units = cp >= 0x10000 ? 2 : 1;
        if (current.size() + units > maxUnits) {
            chunks.push_back(std::move(current));
            current.clear();
        }
        if (units == 2) {
            const char32_t v = cp - 0x10000;
            current.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            current.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            current.push_back(static_cast<char16_t>(cp));
        }
    }
    if (!current.empty()) chunks.push_back(std::move(current));
    return chunks;
}

#ifdef _WIN32

bool isConsoleHandle(void* handle) {
    DWORD mode = 0;
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0;
}

namespace {

// WriteConsoleW fails for a single write of tens of thousands of characters
// on some Windows versions; this stays well under that.
constexpr std::size_t kConsoleChunkUnits = 4096;

bool writeConsoleOrStdio(DWORD which, std::FILE* stream, std::string_view text) {
    HANDLE handle = GetStdHandle(which);
    if (!isConsoleHandle(handle)) return writeStdio(text, stream);
    // Anything already in the stdio buffer must come out first.
    std::fflush(stream);
    for (const std::u16string& chunk : toUtf16Chunks(text, kConsoleChunkUnits)) {
        const wchar_t* data = reinterpret_cast<const wchar_t*>(chunk.data());
        std::size_t left = chunk.size();
        while (left > 0) {
            DWORD written = 0;
            if (!WriteConsoleW(handle, data, static_cast<DWORD>(left), &written, nullptr) ||
                written == 0)
                return false;
            data += written;
            left -= written;
        }
    }
    return true;
}

}  // namespace

bool stdoutIsTerminal() { return isConsoleHandle(GetStdHandle(STD_OUTPUT_HANDLE)); }
bool stderrIsTerminal() { return isConsoleHandle(GetStdHandle(STD_ERROR_HANDLE)); }

std::size_t stderrWidth() {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &info)) {
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        if (width > 0) return static_cast<std::size_t>(width);
    }
    return 80;
}

bool writeStdout(std::string_view text) {
    return writeConsoleOrStdio(STD_OUTPUT_HANDLE, stdout, text);
}
bool writeStderr(std::string_view text) {
    return writeConsoleOrStdio(STD_ERROR_HANDLE, stderr, text);
}

#else

bool stdoutIsTerminal() { return isatty(STDOUT_FILENO) != 0; }
bool stderrIsTerminal() { return isatty(STDERR_FILENO) != 0; }

std::size_t stderrWidth() {
    struct winsize ws {};
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    // getenv races only with setenv/putenv, which this program never calls.
    if (const char* columns = std::getenv("COLUMNS")) {  // NOLINT(concurrency-mt-unsafe)
        const long v = std::strtol(columns, nullptr, 10);
        if (v > 0 && v < 10000) return static_cast<std::size_t>(v);
    }
    return 80;
}

bool writeStdout(std::string_view text) { return writeStdio(text, stdout); }
bool writeStderr(std::string_view text) { return writeStdio(text, stderr); }

#endif

}  // namespace term
}  // namespace mediagrep
