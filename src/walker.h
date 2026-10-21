// Directory traversal and filename filtering. Paths are UTF-8 strings.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mediagrep {

struct WalkOptions {
    bool recurse = false;               // -r or -R; otherwise a directory root is an error
    bool followSymlinks = false;        // -R; symlinks given as roots are always followed
    std::vector<std::string> include;   // --include globs; empty means everything
    std::vector<std::string> exclude;   // --exclude globs
    std::function<bool()> cancelled;    // checked before each entry; stops the walk
    // Something worth saying that is not a failure, so it does not count as an
    // error: a symlink cycle skipped under -R, as grep does. May be empty.
    std::function<void(const std::string& path, const std::string& message)> warning;
};

// Shell-style glob against a file name: '*', '?', '[abc]', '[a-z]', '[!abc]'.
// Case-sensitive. Matches the whole name.
bool globMatch(std::string_view pattern, std::string_view name);

using FileCallback = std::function<void(const std::string& path)>;
using ErrorCallback = std::function<void(const std::string& path, const std::string& message)>;

// Visits regular files under `roots` in a deterministic order: roots in the
// order given, directory entries sorted by name, depth first. Reported paths
// are the root as given joined with the relative part, like grep -r.
// Roots that are files are always visited, ignoring include/exclude.
// Without `recurse`, a directory root is reported as "Is a directory"
// and skipped, like grep.
// Problems (missing root or link target, unreadable directory) go to onError
// and the walk continues. Returns the number of errors.
// With `followSymlinks`, a directory that is already being walked higher up
// the current path (a symlink cycle) is skipped with a "recursive directory
// loop" warning. Reaching the same directory by two routes that are not
// nested is not a loop, and it is walked each time, like grep.
std::size_t walk(const std::vector<std::string>& roots, const WalkOptions& options,
                 const FileCallback& onFile, const ErrorCallback& onError);

}  // namespace mediagrep
