// Directory traversal and filename filtering.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mediagrep {

struct WalkOptions {
    bool followSymlinks = false;        // -R; symlinks given as roots are always followed
    std::vector<std::string> include;   // --include globs; empty means everything
    std::vector<std::string> exclude;   // --exclude globs
};

// Shell-style glob against a file name: '*', '?', '[abc]', '[a-z]', '[!abc]'.
// Case-sensitive. Matches the whole name.
bool globMatch(std::string_view pattern, std::string_view name);

using FileCallback = std::function<void(const std::filesystem::path&)>;
using ErrorCallback = std::function<void(const std::filesystem::path&, const std::string& message)>;

// Visits regular files under `roots` in a deterministic order: roots in the
// order given, directory entries sorted by name, depth first. Reported paths
// are the root as given joined with the relative part, like grep -r.
// Roots that are files are always visited, ignoring include/exclude.
// Problems (missing root, unreadable directory) go to onError and the walk
// continues. Returns the number of errors.
std::size_t walk(const std::vector<std::filesystem::path>& roots, const WalkOptions& options,
                 const FileCallback& onFile, const ErrorCallback& onError);

}  // namespace mediagrep
