#include "term.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mediagrep {
namespace term {

#ifdef _WIN32

bool stdoutIsTerminal() { return _isatty(_fileno(stdout)) != 0; }
bool stderrIsTerminal() { return _isatty(_fileno(stderr)) != 0; }

std::size_t stderrWidth() {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &info)) {
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        if (width > 0) return static_cast<std::size_t>(width);
    }
    return 80;
}

namespace {
UINT previousCodePage = 0;

void restoreCodePage() {
    if (previousCodePage) SetConsoleOutputCP(previousCodePage);
}

BOOL WINAPI onConsoleControl(DWORD) {
    restoreCodePage();
    return FALSE;  // let the default handler end the process
}
}  // namespace

void useUtf8Console() {
    const UINT current = GetConsoleOutputCP();
    if (current == 0 || current == CP_UTF8) return;  // no console, or already UTF-8
    if (!SetConsoleOutputCP(CP_UTF8)) return;
    previousCodePage = current;
    std::atexit(restoreCodePage);
    SetConsoleCtrlHandler(onConsoleControl, TRUE);
}

#else

bool stdoutIsTerminal() { return isatty(STDOUT_FILENO) != 0; }
bool stderrIsTerminal() { return isatty(STDERR_FILENO) != 0; }

std::size_t stderrWidth() {
    struct winsize ws {};
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    if (const char* columns = std::getenv("COLUMNS")) {
        const long v = std::strtol(columns, nullptr, 10);
        if (v > 0 && v < 10000) return static_cast<std::size_t>(v);
    }
    return 80;
}

void useUtf8Console() {}

#endif

}  // namespace term
}  // namespace mediagrep
