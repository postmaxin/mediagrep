#include "fsys.h"

#include <system_error>

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
#include <cerrno>
#include <cstring>
// Our fsys::stat() function shadows the struct's name inside the namespace.
using StatBuf = struct stat;
#endif

namespace mediagrep::fsys {

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
        if (u.QuadPart == 0) return std::nullopt;
        // FILETIME counts 100 ns intervals since 1601-01-01.
        return static_cast<std::time_t>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
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
bool isSeparator(char c) { return c == '/' || c == '\\'; }
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

bool isSymlink(const std::string& path) {
    const DWORD attrs = GetFileAttributesW(widen(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool listDirectory(const std::string& path, std::vector<std::string>& names, std::string* error) {
    std::wstring pattern = widen(path);
    if (pattern.empty() || !isSeparator(path.back())) pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(pattern.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) *error = lastErrorMessage();
        return false;
    }
    do {
        const std::string name = narrow(data.cFileName);
        if (name != "." && name != "..") names.push_back(name);
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return true;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (isSeparator(dir.back())) return dir + name;
    return dir + '\\' + name;
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
    if (const passwd* pw = ::getpwuid(st.st_uid)) fi.owner = pw->pw_name;
    if (const ::group* gr = ::getgrgid(st.st_gid)) fi.group = gr->gr_name;
    return fi;
}

FileType status(const std::string& path, std::string* error) {
    StatBuf st;
    if (::stat(path.c_str(), &st) != 0) {
        if (errno != ENOENT && errno != ENOTDIR && error) *error = std::strerror(errno);
        return FileType::Missing;
    }
    if (S_ISDIR(st.st_mode)) return FileType::Directory;
    if (S_ISREG(st.st_mode)) return FileType::Regular;
    return FileType::Other;
}

bool isSymlink(const std::string& path) {
    StatBuf st;
    return ::lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

bool listDirectory(const std::string& path, std::vector<std::string>& names, std::string* error) {
    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        if (error) *error = std::strerror(errno);
        return false;
    }
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") names.push_back(name);
    }
    ::closedir(dir);
    return true;
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
