// A throwaway directory tree for walker tests, without std::filesystem.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fsys.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include "utf.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mediagrep::test {

inline void makeDir(const std::string& p) {
#ifdef _WIN32
    _wmkdir(widen(p).c_str());
#else
    ::mkdir(p.c_str(), 0700);
#endif
}

inline void removeFile(const std::string& p) {
#ifdef _WIN32
    _wremove(widen(p).c_str());
#else
    ::unlink(p.c_str());
#endif
}

inline void removeDir(const std::string& p) {
#ifdef _WIN32
    _wrmdir(widen(p).c_str());
#else
    ::rmdir(p.c_str());
#endif
}

inline void touch(const std::string& p) {
#ifdef _WIN32
    FILE* f = _wfopen(widen(p).c_str(), L"wb");
#else
    FILE* f = std::fopen(p.c_str(), "wb");
#endif
    if (f) { std::fputs("x", f); std::fclose(f); }
}

inline void removeTree(const std::string& p) {
    std::vector<std::string> names;
    if (fsys::listDirectory(p, names)) {
        for (const auto& n : names) {
            const std::string child = fsys::join(p, n);
            if (fsys::status(child) == fsys::FileType::Directory) removeTree(child);
            else removeFile(child);
        }
        removeDir(p);
    } else {
        removeFile(p);
    }
}

inline std::string tempBase() {
    for (const char* var : {"TMPDIR", "TEMP", "TMP"})
        if (const char* v = std::getenv(var)) return v;
#ifdef _WIN32
    return ".";
#else
    return "/tmp";
#endif
}

// Creates <temp>/<name>-<n> fresh, and removes it on destruction.
struct TempTree {
    std::string root;
    explicit TempTree(const std::string& name) {
        static int counter = 0;
        root = fsys::join(tempBase(), name + "-" + std::to_string(counter++));
        removeTree(root);
        makeDir(root);
    }
    ~TempTree() { removeTree(root); }
    // rel uses '/' separators.
    std::string path(const std::string& rel) const {
        std::string out = root;
        std::string part;
        for (char c : rel + "/") {
            if (c == '/') { if (!part.empty()) out = fsys::join(out, part); part.clear(); }
            else part += c;
        }
        return out;
    }
    void dir(const std::string& rel) {
        std::string acc;
        std::string part;
        for (char c : rel + "/") {
            if (c == '/') {
                if (!part.empty()) { acc = acc.empty() ? part : acc + "/" + part; makeDir(path(acc)); }
                part.clear();
            } else part += c;
        }
    }
    void file(const std::string& rel) {
        const std::size_t slash = rel.find_last_of('/');
        if (slash != std::string::npos) dir(rel.substr(0, slash));
        touch(path(rel));
    }
};

}  // namespace mediagrep::test
