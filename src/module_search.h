// Where mediagrep may load MediaInfoLib from at runtime.
//
// The vendored MediaInfoDLL.h tries, among others, "./libmediainfo.so.0",
// which is relative to the current directory. Run in a directory someone
// else controls, that loads their library before any file is opened. The
// vendored header stays unpatched: mediainfo_source.cpp routes its
// dlopen/LoadLibrary calls through the functions below, which refuse any
// location that depends on the current directory.
//
// POSIX and macOS: a bare soname (the system search path, LD_LIBRARY_PATH,
// rpath), an absolute path (the /usr/local/lib and /usr/lib fallbacks, or
// the path given with MEDIAGREP_MEDIAINFO_LIBRARY), or a name under
// @executable_path/ (next to the binary).
//
// Windows: the application directory and the system directories first
// (LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS),
// then plain LoadLibrary so a DLL on PATH still works. Before that
// fallback SetDllDirectoryW(L"") removes the current directory from the
// standard search order, which is then: the application directory, the
// system directories, the Windows directory, PATH. Never the current
// directory.
#pragma once

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mediagrep {

// True if dlopen(name) cannot reach into the current directory: a name
// without a slash, an absolute path, or under @executable_path/.
inline bool isTrustedModuleName(const char* name) {
    if (!name || !*name) return false;
    if (!std::strchr(name, '/')) return true;
    if (name[0] == '/') return true;
    constexpr char kExecutableDir[] = "@executable_path/";
    return std::strncmp(name, kExecutableDir, sizeof kExecutableDir - 1) == 0;
}

#ifdef _WIN32

// An empty string removes the current directory from the standard search
// order for the rest of the process (see SetDllDirectory in the Windows
// docs). Called before every fallback; it is cheap and idempotent.
inline void dropCurrentDirectoryFromSearch() { ::SetDllDirectoryW(L""); }

inline HMODULE loadModule(const wchar_t* name) {
    HMODULE m = ::LoadLibraryExW(name, nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m) return m;
    dropCurrentDirectoryFromSearch();
    return ::LoadLibraryW(name);
}

inline HMODULE loadModule(const char* name) {
    HMODULE m = ::LoadLibraryExA(name, nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m) return m;
    dropCurrentDirectoryFromSearch();
    return ::LoadLibraryA(name);
}

#else

inline void* loadModule(const char* name, int flags) {
    return isTrustedModuleName(name) ? ::dlopen(name, flags) : nullptr;
}

#endif

}  // namespace mediagrep
