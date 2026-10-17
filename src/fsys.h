// Minimal file-system access on UTF-8 paths, with POSIX and Win32
// backends. std::filesystem is avoided on purpose: toolchains such as
// Entware's GCC 8 ship no implementation of it, and NAS boxes are exactly
// where a media collection lives.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mediagrep::fsys {

enum class FileType { Missing, Regular, Directory, Other };

// Type of the file at `path`, following symlinks. On failure (other than
// a missing file, which is simply Missing) sets `error` to the OS message.
FileType status(const std::string& path, std::string* error = nullptr);

// True if `path` itself is a symbolic link (not followed).
bool isSymlink(const std::string& path);

// Fills `names` with the entries of a directory, excluding "." and "..",
// in no particular order. Returns false and sets `error` on failure.
bool listDirectory(const std::string& path, std::vector<std::string>& names,
                   std::string* error = nullptr);

// dir + separator + name, without doubling a separator `dir` already ends
// with. An empty `dir` yields `name` alone.
std::string join(const std::string& dir, const std::string& name);

// The last path component. Both '/' and, on Windows, '\\' separate.
std::string fileName(std::string_view path);

}  // namespace mediagrep::fsys
