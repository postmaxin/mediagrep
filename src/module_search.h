// Where mediagrep may load MediaInfoLib from at runtime.
//
// The vendored MediaInfoDLL.h tries, among others, "./libmediainfo.so.0",
// which is relative to the current directory. Run in a directory someone
// else controls, that loads their library before any file is opened. The
// vendored header stays unpatched: mediainfo_source.cpp routes its
// dlopen/LoadLibrary calls through the functions below, which refuse any
// location that depends on the current directory.
//
// POSIX: a bare soname (the system search path, LD_LIBRARY_PATH, rpath), an
// absolute path (the /usr/local/lib and /usr/lib fallbacks, or the path
// given with MEDIAGREP_MEDIAINFO_LIBRARY), or a name under
// @executable_path/ (next to the binary).
//
// macOS: Apple's dlopen searches the current directory for a leaf name, so a
// bare name is never passed through. It is resolved here against fixed
// trusted directories (next to the binary, /opt/homebrew/lib,
// /usr/local/lib, /usr/lib) and the absolute path is opened.
//
// Windows: names are validated first: a bare file name or a fully qualified
// path (X:\... or \\server\...) only; .\x, ..\x, dir\x and X:x are refused.
// Then the application directory and the system directories
// (LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS),
// then plain LoadLibrary so a DLL on PATH still works. Before that
// fallback SetDllDirectoryW(L"") removes the current directory from the
// standard search order, which is then: the application directory, the
// system directories, the Windows directory, PATH. Never the current
// directory.
#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mediagrep {

#ifdef __APPLE__
constexpr bool kAppleLoader = true;
#else
constexpr bool kAppleLoader = false;
#endif

// True if dlopen(name) cannot reach into the current directory: an absolute
// path; elsewhere a name without a slash; on Apple platforms a name under
// @executable_path/, which only dyld understands (elsewhere it would be an
// ordinary relative directory). On Apple platforms a leaf name is not trusted
// as is (see moduleCandidates()).
inline bool isTrustedModuleName(const char* name, bool apple = kAppleLoader) {
    if (!name || !*name) return false;
    if (!std::strchr(name, '/')) return !apple;
    if (name[0] == '/') return true;
    constexpr char kExecutableDir[] = "@executable_path/";
    return apple && std::strncmp(name, kExecutableDir, sizeof kExecutableDir - 1) == 0;
}

// The names to hand to dlopen, in order; empty when the name is refused. On
// Apple platforms a leaf name expands to trusted absolute locations.
inline std::vector<std::string> moduleCandidates(const char* name, bool apple = kAppleLoader) {
    if (!name || !*name) return {};
    if (apple && !std::strchr(name, '/')) {
        const std::string leaf = name;
        return {"@executable_path/" + leaf, "/opt/homebrew/lib/" + leaf, "/usr/local/lib/" + leaf,
                "/usr/lib/" + leaf};
    }
    if (isTrustedModuleName(name, apple)) return {name};
    return {};
}

// Windows: a bare file name, or a fully qualified path (a drive letter and a
// separator, or a UNC or extended-length prefix). Directory-relative (.\x,
// ..\x, dir\x) and drive-relative (X:x) names are refused. Pure, so it is
// tested on every platform.
template <class Char>
bool isTrustedWindowsModuleName(std::basic_string_view<Char> name) {
    auto sep = [](Char c) { return c == Char('\\') || c == Char('/'); };
    auto letter = [](Char c) {
        return (c >= Char('A') && c <= Char('Z')) || (c >= Char('a') && c <= Char('z'));
    };
    if (name.empty()) return false;
    if (name.size() >= 2 && sep(name[0]) && sep(name[1])) return true;  // UNC, extended-length
    if (name.size() >= 3 && letter(name[0]) && name[1] == Char(':') && sep(name[2])) return true;
    for (Char c : name)
        if (sep(c) || c == Char(':')) return false;
    return true;
}

#ifdef _WIN32

// An empty string removes the current directory from the standard search
// order for the rest of the process (see SetDllDirectory in the Windows
// docs). Called before every fallback; it is cheap and idempotent.
inline void dropCurrentDirectoryFromSearch() { ::SetDllDirectoryW(L""); }

inline HMODULE loadModule(const wchar_t* name) {
    if (!name || !isTrustedWindowsModuleName(std::wstring_view(name))) return nullptr;
    HMODULE m = ::LoadLibraryExW(name, nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m) return m;
    dropCurrentDirectoryFromSearch();
    return ::LoadLibraryW(name);
}

inline HMODULE loadModule(const char* name) {
    if (!name || !isTrustedWindowsModuleName(std::string_view(name))) return nullptr;
    HMODULE m = ::LoadLibraryExA(name, nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m) return m;
    dropCurrentDirectoryFromSearch();
    return ::LoadLibraryA(name);
}

#else

inline void* loadModule(const char* name, int flags) {
    for (const std::string& candidate : moduleCandidates(name))
        if (void* handle = ::dlopen(candidate.c_str(), flags)) return handle;
    return nullptr;
}

#endif

}  // namespace mediagrep
