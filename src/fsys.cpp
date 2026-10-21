#include "fsys.h"

#include <limits>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "utf.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
// Our fsys::stat() function shadows the struct's name inside the namespace.
using StatBuf = struct stat;
#endif

namespace mediagrep::fsys {

#ifndef _WIN32
namespace {

// glibc's strerror_r (GNU flavour) returns the message, which may not be in
// the buffer; POSIX's returns 0 or an error code and fills the buffer.
// Overloads pick whichever the C library declared.
[[maybe_unused]] const char* strerrorResult(const char* message, const char*) { return message; }
[[maybe_unused]] const char* strerrorResult(int rc, const char* buffer) {
    return rc == 0 ? buffer : nullptr;
}

// Name of a user or group from getpwuid_r/getgrgid_r, which unlike getpwuid
// and getgrgid do not return a pointer into a shared static buffer: the
// worker threads call these concurrently. The buffer grows on ERANGE.
// Empty if there is no such entry.
template <class Entry, class Id, class Lookup>
std::string lookupName(long sizeHint, Id id, Lookup lookup, char* Entry::*name) {
    std::vector<char> buffer(sizeHint > 0 ? static_cast<std::size_t>(sizeHint) : 1024);
    for (;;) {
        Entry entry;
        Entry* found = nullptr;
        const int rc = lookup(id, &entry, buffer.data(), buffer.size(), &found);
        if (rc == ERANGE && buffer.size() < (1u << 20)) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        return rc == 0 && found && found->*name ? std::string(found->*name) : std::string();
    }
}

}  // namespace
#endif

std::string errorMessage(int err) {
#ifdef _WIN32
    return std::generic_category().message(err);
#else
    char buffer[256] = {};
    const char* message = strerrorResult(::strerror_r(err, buffer, sizeof buffer), buffer);
    return message && *message ? message : "error " + std::to_string(err);
#endif
}

namespace detail {

bool collectEntries(const EntryReader& next, std::vector<std::string>& names, std::string* error) {
    for (;;) {
        int err = 0;
        std::optional<std::string> name = next(err);
        if (!name) {
            if (err == 0) return true;
            if (error) *error = std::system_category().message(err);
            return false;
        }
        if (*name != "." && *name != "..") names.push_back(std::move(*name));
    }
}

bool findFailureEndsListing(unsigned long code, bool first) {
    constexpr unsigned long kFileNotFound = 2;   // ERROR_FILE_NOT_FOUND
    constexpr unsigned long kNoMoreFiles = 18;   // ERROR_NO_MORE_FILES
    return code == kNoMoreFiles || (first && code == kFileNotFound);
}

std::optional<std::time_t> fileTimeToUnix(std::uint64_t ticks) {
    if (ticks == 0) return std::nullopt;
    constexpr std::int64_t kSecondsFrom1601To1970 = 11644473600;
    // Whole seconds first, then the signed offset, so dates before 1970 come out negative.
    const std::int64_t seconds =
        static_cast<std::int64_t>(ticks / 10000000ULL) - kSecondsFrom1601To1970;
    // A 32-bit time_t covers only 1901-2038; outside that the time is unknown,
    // not a wrapped-around date.
    if (seconds < static_cast<std::int64_t>(std::numeric_limits<std::time_t>::min()) ||
        seconds > static_cast<std::int64_t>(std::numeric_limits<std::time_t>::max()))
        return std::nullopt;
    return static_cast<std::time_t>(seconds);
}

bool isLinkReparseTag(std::uint32_t tag) {
    constexpr std::uint32_t kNameSurrogate = 0x20000000;  // IsReparseTagNameSurrogate
    return (tag & kNameSurrogate) != 0;
}

namespace {
bool isWindowsSeparator(char c) { return c == '/' || c == '\\'; }
bool isBareDrive(const std::string& path) {
    return path.size() == 2 && path[1] == ':' &&
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}
}  // namespace

std::string windowsJoin(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (isWindowsSeparator(dir.back()) || isBareDrive(dir)) return dir + name;
    return dir + '\\' + name;
}

std::string windowsSearchPattern(const std::string& dir) {
    return windowsJoin(dir, "*");
}

}  // namespace detail

#ifdef _WIN32

std::optional<FileInfo> stat(const std::string& path) {
    HANDLE h = CreateFileW(widen(path).c_str(), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!ok) return std::nullopt;

    auto toTime = [](const FILETIME& ft) -> std::optional<std::time_t> {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return detail::fileTimeToUnix(u.QuadPart);
    };
    FileInfo fi;
    fi.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    fi.modified = toTime(info.ftLastWriteTime);
    fi.created = toTime(info.ftCreationTime);
    fi.accessed = toTime(info.ftLastAccessTime);
    return fi;  // mode, owner and group have no faithful Windows equivalent
}

namespace {
std::string lastErrorMessage() {
    return std::system_category().message(static_cast<int>(GetLastError()));
}
}  // namespace

FileType status(const std::string& path, std::string* error) {
    // GetFileAttributes does not follow symlinks; opening the file does.
    HANDLE h = CreateFileW(widen(path).c_str(), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND && e != ERROR_INVALID_NAME && error)
            *error = std::system_category().message(static_cast<int>(e));
        return FileType::Missing;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!ok) {
        if (error) *error = lastErrorMessage();
        return FileType::Missing;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return FileType::Directory;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) return FileType::Other;
    return FileType::Regular;
}

std::optional<FileId> directoryId(const std::string& path, std::string* error) {
    HANDLE h = CreateFileW(widen(path).c_str(), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h, &info)) {
        if (error) *error = lastErrorMessage();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return std::nullopt;
    }
    CloseHandle(h);
    return FileId{info.dwVolumeSerialNumber,
                  (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

bool isSymlink(const std::string& path) {
    const std::wstring wide = widen(path);
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    // Reparse points also mark OneDrive placeholders, dedup files and the like.
    // Those are ordinary files; only the tag tells them from links.
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(wide.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return true;  // cannot tell, so skip it as before
    FindClose(h);
    return detail::isLinkReparseTag(data.dwReserved0);
}

bool listDirectory(const std::string& path, std::vector<std::string>& names, std::string* error) {
    const std::wstring pattern = widen(detail::windowsSearchPattern(path));
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(pattern.c_str(), &data);
    bool first = true;
    // A failure ends the listing only if it says so; anything else (a network
    // error on an SMB share) must not look like a shorter directory.
    const bool ok = detail::collectEntries(
        [&](int& err) -> std::optional<std::string> {
            err = 0;
            const bool wasFirst = std::exchange(first, false);
            if (wasFirst ? h == INVALID_HANDLE_VALUE : !FindNextFileW(h, &data)) {
                const DWORD code = GetLastError();
                if (!detail::findFailureEndsListing(code, wasFirst)) err = static_cast<int>(code);
                return std::nullopt;
            }
            return narrow(data.cFileName);
        },
        names, error);
    if (h != INVALID_HANDLE_VALUE) FindClose(h);
    return ok;
}

std::string join(const std::string& dir, const std::string& name) {
    return detail::windowsJoin(dir, name);
}

std::string fileName(std::string_view path) {
    const std::size_t pos = path.find_last_of("/\\");
    return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
}

#else

std::optional<FileInfo> stat(const std::string& path) {
    StatBuf st;
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    FileInfo fi;
    fi.size = static_cast<std::uint64_t>(st.st_size);
    fi.modified = st.st_mtime;
    fi.accessed = st.st_atime;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    fi.created = st.st_birthtime;
#elif defined(__linux__) && defined(STATX_BTIME)
    struct statx sx;
    if (::statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME))
        fi.created = static_cast<std::time_t>(sx.stx_btime.tv_sec);
#endif
    fi.mode = static_cast<unsigned>(st.st_mode & 07777);
    fi.owner = lookupName<passwd>(::sysconf(_SC_GETPW_R_SIZE_MAX), st.st_uid, ::getpwuid_r,
                                  &passwd::pw_name);
    fi.group = lookupName<::group>(::sysconf(_SC_GETGR_R_SIZE_MAX), st.st_gid, ::getgrgid_r,
                                   &::group::gr_name);
    return fi;
}

FileType status(const std::string& path, std::string* error) {
    StatBuf st;
    if (::stat(path.c_str(), &st) != 0) {
        if (errno != ENOENT && errno != ENOTDIR && error) *error = errorMessage(errno);
        return FileType::Missing;
    }
    if (S_ISDIR(st.st_mode)) return FileType::Directory;
    if (S_ISREG(st.st_mode)) return FileType::Regular;
    return FileType::Other;
}

std::optional<FileId> directoryId(const std::string& path, std::string* error) {
    StatBuf st;
    if (::stat(path.c_str(), &st) != 0) {
        if (error) *error = errorMessage(errno);
        return std::nullopt;
    }
    return FileId{static_cast<std::uint64_t>(st.st_dev), static_cast<std::uint64_t>(st.st_ino)};
}

bool isSymlink(const std::string& path) {
    StatBuf st;
    return ::lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

bool listDirectory(const std::string& path, std::vector<std::string>& names, std::string* error) {
    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        if (error) *error = errorMessage(errno);
        return false;
    }
    // readdir returns null at the end and on error alike; only errno tells which.
    // It is safe across threads when each uses its own DIR*, as here.
    const bool ok = detail::collectEntries(
        [&](int& err) -> std::optional<std::string> {
            errno = 0;
            const dirent* entry = ::readdir(dir);  // NOLINT(concurrency-mt-unsafe)
            if (!entry) {
                err = errno;
                return std::nullopt;
            }
            return std::string(entry->d_name);
        },
        names, error);
    ::closedir(dir);
    return ok;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + '/' + name;
}

std::string fileName(std::string_view path) {
    const std::size_t pos = path.find_last_of('/');
    return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
}

#endif

}  // namespace mediagrep::fsys
