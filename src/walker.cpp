#include "walker.h"

#include <algorithm>
#include <optional>

#include "fsys.h"

namespace mediagrep {

namespace {

// Matches one bracket expression starting at pattern[start] == '['.
// On success sets `end` to the index after the closing ']'.
// Returns nullopt if the expression is unterminated.
std::optional<bool> matchClass(std::string_view pattern, std::size_t start, char ch, std::size_t& end) {
    std::size_t i = start + 1;
    bool negate = false;
    if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^')) { negate = true; ++i; }
    bool matched = false;
    bool first = true;
    while (i < pattern.size() && (pattern[i] != ']' || first)) {
        first = false;
        const char lo = pattern[i];
        if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']') {
            const char hi = pattern[i + 2];
            if (lo <= ch && ch <= hi) matched = true;
            i += 3;
        } else {
            if (ch == lo) matched = true;
            ++i;
        }
    }
    if (i >= pattern.size()) return std::nullopt;
    end = i + 1;
    return matched != negate;
}

struct Walker {
    const WalkOptions& options;
    const FileCallback& onFile;
    const ErrorCallback& onError;
    std::size_t errors = 0;

    bool passesFilters(const std::string& name) const {
        if (!options.include.empty()) {
            const bool any = std::any_of(options.include.begin(), options.include.end(),
                                         [&](const std::string& g) { return globMatch(g, name); });
            if (!any) return false;
        }
        return std::none_of(options.exclude.begin(), options.exclude.end(),
                            [&](const std::string& g) { return globMatch(g, name); });
    }

    void fail(const std::string& path, const std::string& message) {
        ++errors;
        onError(path, message);
    }

    bool stopped() const { return options.cancelled && options.cancelled(); }

    void root(const std::string& path) {
        if (stopped()) return;
        std::string error;
        const fsys::FileType type = fsys::status(path, &error);
        if (!error.empty()) { fail(path, error); return; }
        switch (type) {
            case fsys::FileType::Missing: fail(path, "No such file or directory"); break;
            case fsys::FileType::Directory:
                if (options.recurse) directory(path);
                else fail(path, "Is a directory");
                break;
            case fsys::FileType::Regular: onFile(path); break;
            case fsys::FileType::Other: break;
        }
    }

    void directory(const std::string& dir) {
        std::vector<std::string> names;
        std::string error;
        if (!fsys::listDirectory(dir, names, &error)) { fail(dir, error); return; }
        std::sort(names.begin(), names.end());

        for (const std::string& name : names) {
            if (stopped()) return;
            const std::string child = fsys::join(dir, name);
            if (!options.followSymlinks && fsys::isSymlink(child)) continue;
            error.clear();
            const fsys::FileType type = fsys::status(child, &error);
            if (!error.empty()) { fail(child, error); continue; }
            if (type == fsys::FileType::Directory) directory(child);
            else if (type == fsys::FileType::Regular && passesFilters(name)) onFile(child);
        }
    }
};

}  // namespace

bool globMatch(std::string_view pattern, std::string_view name) {
    constexpr std::size_t npos = std::string_view::npos;
    std::size_t pi = 0, si = 0;
    std::size_t starP = npos, starS = 0;
    while (si < name.size()) {
        if (pi < pattern.size()) {
            const char c = pattern[pi];
            if (c == '*') { starP = pi++; starS = si; continue; }
            if (c == '?') { ++pi; ++si; continue; }
            if (c == '[') {
                std::size_t end = 0;
                const auto m = matchClass(pattern, pi, name[si], end);
                if (m) {
                    if (*m) { pi = end; ++si; continue; }
                } else if (name[si] == '[') {  // unterminated: literal
                    ++pi; ++si; continue;
                }
            } else if (c == name[si]) {
                ++pi; ++si; continue;
            }
        }
        if (starP == npos) return false;
        pi = starP + 1;
        si = ++starS;
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

std::size_t walk(const std::vector<std::string>& roots, const WalkOptions& options,
                 const FileCallback& onFile, const ErrorCallback& onError) {
    Walker w{options, onFile, onError};
    for (const auto& r : roots) w.root(r);
    return w.errors;
}

}  // namespace mediagrep
