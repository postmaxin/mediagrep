#include "walker.h"

#include <algorithm>
#include <optional>
#include <system_error>

#include "utf.h"

namespace mediagrep {

namespace fs = std::filesystem;

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

std::string fileName(const fs::path& p) {
#ifdef _WIN32
    return narrow(p.filename().wstring());
#else
    return p.filename().native();
#endif
}

struct Walker {
    const WalkOptions& options;
    const FileCallback& onFile;
    const ErrorCallback& onError;
    std::size_t errors = 0;

    bool passesFilters(const fs::path& p) const {
        const std::string name = fileName(p);
        if (!options.include.empty()) {
            const bool any = std::any_of(options.include.begin(), options.include.end(),
                                         [&](const std::string& g) { return globMatch(g, name); });
            if (!any) return false;
        }
        return std::none_of(options.exclude.begin(), options.exclude.end(),
                            [&](const std::string& g) { return globMatch(g, name); });
    }

    void fail(const fs::path& p, const std::string& message) {
        ++errors;
        onError(p, message);
    }

    void root(const fs::path& p) {
        std::error_code ec;
        const fs::file_status st = fs::status(p, ec);
        if (ec) { fail(p, ec.message()); return; }
        if (!fs::exists(st)) { fail(p, "No such file or directory"); return; }
        if (fs::is_directory(st)) directory(p);
        else if (fs::is_regular_file(st)) onFile(p);
    }

    void directory(const fs::path& dir) {
        std::error_code ec;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) { fail(dir, ec.message()); return; }

        std::vector<fs::directory_entry> entries;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { fail(dir, ec.message()); return; }
            entries.push_back(*it);
        }
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename().native() < b.path().filename().native();
        });

        for (const auto& e : entries) {
            if (!options.followSymlinks && e.is_symlink(ec)) continue;
            const fs::file_status st = e.status(ec);
            if (ec) { fail(e.path(), ec.message()); continue; }
            if (fs::is_directory(st)) directory(e.path());
            else if (fs::is_regular_file(st) && passesFilters(e.path())) onFile(e.path());
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

std::size_t walk(const std::vector<fs::path>& roots, const WalkOptions& options,
                 const FileCallback& onFile, const ErrorCallback& onError) {
    Walker w{options, onFile, onError};
    for (const auto& r : roots) w.root(r);
    return w.errors;
}

}  // namespace mediagrep
