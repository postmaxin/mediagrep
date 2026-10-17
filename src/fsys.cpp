#include "fsys.h"

#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "utf.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#endif

namespace mediagrep::fsys {

#ifdef _WIN32

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

FileType status(const std::string& path, std::string* error) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        if (errno != ENOENT && errno != ENOTDIR && error) *error = std::strerror(errno);
        return FileType::Missing;
    }
    if (S_ISDIR(st.st_mode)) return FileType::Directory;
    if (S_ISREG(st.st_mode)) return FileType::Regular;
    return FileType::Other;
}

bool isSymlink(const std::string& path) {
    struct stat st;
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
