// Minimal file-system access on UTF-8 paths, with POSIX and Win32
// backends. std::filesystem is avoided on purpose: toolchains such as
// Entware's GCC 8 ship no implementation of it, and NAS boxes are exactly
// where a media collection lives.
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mediagrep::fsys {

enum class FileType { Missing, Regular, Directory, Other };

// Type of the file at `path`, following symlinks. On failure (other than
// a missing file, which is simply Missing) sets `error` to the OS message.
FileType status(const std::string& path, std::string* error = nullptr);

// What identifies a file system object: the volume (device, or volume serial
// number) and the object on it (inode, or file index). Two paths with the same
// FileId are the same directory, however they were reached.
struct FileId {
    std::uint64_t volume = 0;
    std::uint64_t index = 0;
    bool operator==(const FileId& o) const { return volume == o.volume && index == o.index; }
};

// Identity of the directory at `path`, following symlinks. nullopt (and `error`
// set) if it cannot be examined. Lets a recursive walk spot a symlink cycle.
std::optional<FileId> directoryId(const std::string& path, std::string* error = nullptr);

// True if `path` itself is a symbolic link (not followed).
bool isSymlink(const std::string& path);

// Fills `names` with the entries of a directory, excluding "." and "..",
// in no particular order. Returns false and sets `error` on failure.
bool listDirectory(const std::string& path, std::vector<std::string>& names,
                   std::string* error = nullptr);

struct FileInfo {
    std::uint64_t size = 0;
    std::optional<std::time_t> modified;
    std::optional<std::time_t> created;   // birth time, where the OS keeps one
    std::optional<std::time_t> accessed;
    std::optional<unsigned> mode;         // permission bits (0644); POSIX only
    std::string owner;                    // empty if unknown
    std::string group;                    // empty if unknown
};

// stat() following symlinks. nullopt if the file cannot be examined.
std::optional<FileInfo> stat(const std::string& path);

// The message for an errno value, like strerror() but safe to call from
// several threads at once.
std::string errorMessage(int err);

// dir + separator + name, without doubling a separator `dir` already ends
// with. An empty `dir` yields `name` alone.
std::string join(const std::string& dir, const std::string& name);

// The last path component. Both '/' and, on Windows, '\\' separate.
std::string fileName(std::string_view path);

}  // namespace mediagrep::fsys
