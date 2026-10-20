// The few terminal facts mediagrep needs, on POSIX and Windows.
#pragma once

#include <cstddef>

namespace mediagrep {
namespace term {

bool stdoutIsTerminal();
bool stderrIsTerminal();

// Columns of the terminal stderr is attached to; COLUMNS, then 80, when
// that cannot be determined.
std::size_t stderrWidth();

// On Windows, switches the console to UTF-8 output for the life of the
// process so file names print correctly. Elsewhere does nothing.
void useUtf8Console();

}  // namespace term
}  // namespace mediagrep
