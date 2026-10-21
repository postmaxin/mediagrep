// The few terminal facts mediagrep needs, on POSIX and Windows, and the
// functions every byte of stdout and stderr output goes through.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mediagrep {
namespace term {

// True when the stream is attached to a terminal. On Windows that means a
// console: a handle GetConsoleMode accepts, so NUL is not a terminal.
bool stdoutIsTerminal();
bool stderrIsTerminal();

// Columns of the terminal stderr is attached to; COLUMNS, then 80, when
// that cannot be determined.
std::size_t stderrWidth();

// Writes `text`, which is UTF-8, to stdout or stderr; false if that failed.
// Redirected output (a file or pipe) and all output on POSIX is the bytes
// as given, through stdio. On a Windows console the text is converted to
// UTF-16 and written with WriteConsoleW, so file names print correctly in
// any code page and the console's code page, which belongs to every
// process sharing the console, is never changed.
bool writeStdout(std::string_view text);
bool writeStderr(std::string_view text);

// UTF-8 to UTF-16 in pieces of at most `maxUnits` code units (at least 2),
// for APIs that limit the size of one write. A surrogate pair is never
// split between pieces. A malformed byte becomes U+FFFD. Platform
// independent, so it is tested everywhere; the Windows console path uses it.
std::vector<std::u16string> toUtf16Chunks(std::string_view utf8, std::size_t maxUnits);

#ifdef _WIN32
// Whether `handle` (a Win32 HANDLE, passed as void* to keep windows.h out of
// this header) is a console, as opposed to a file, pipe or NUL.
bool isConsoleHandle(void* handle);
#endif

}  // namespace term
}  // namespace mediagrep
