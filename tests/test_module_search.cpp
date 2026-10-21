// The runtime loader must never reach into the current directory
// (issue #29): the vendored header's "./libmediainfo.so.0" fallback is
// refused by the guard that mediainfo_source.cpp installs. The decisions are
// pure functions, so every platform tests every platform's rules.
#include <string>
#include <vector>

#include "doctest.h"
#include "module_search.h"

using namespace mediagrep;

using Names = std::vector<std::string>;

TEST_CASE("module names that depend on the current directory are refused") {
    for (const bool apple : {false, true}) {
        CAPTURE(apple);
        CHECK_FALSE(isTrustedModuleName("./libmediainfo.so.0", apple));
        CHECK_FALSE(isTrustedModuleName("../libmediainfo.so.0", apple));
        CHECK_FALSE(isTrustedModuleName("lib/libmediainfo.so.0", apple));
        CHECK_FALSE(isTrustedModuleName("@loader_path/libmediainfo.0.dylib", apple));
        CHECK_FALSE(isTrustedModuleName("@executable_pathx/libmediainfo.0.dylib", apple));
        CHECK_FALSE(isTrustedModuleName("", apple));
        CHECK_FALSE(isTrustedModuleName(nullptr, apple));
        CHECK(moduleCandidates("./libmediainfo.so.0", apple).empty());
        CHECK(moduleCandidates("sub/libmediainfo.so.0", apple).empty());
    }
}

TEST_CASE("sonames, absolute paths and the executable directory are accepted") {
    CHECK(isTrustedModuleName("libmediainfo.so.0", false));
    CHECK(isTrustedModuleName("/usr/lib/libmediainfo.so.0", false));
    CHECK(isTrustedModuleName("/usr/local/lib64/libmediainfo.so.0", false));
    CHECK(isTrustedModuleName("@executable_path/libmediainfo.0.dylib", false));
    CHECK(moduleCandidates("libmediainfo.so.0", false) == Names{"libmediainfo.so.0"});
    CHECK(moduleCandidates("/usr/lib/libmediainfo.so.0", false) ==
          Names{"/usr/lib/libmediainfo.so.0"});
}

TEST_CASE("on Apple platforms a leaf name is resolved against trusted directories") {
    // Apple's dlopen would search the current directory for a bare name.
    CHECK_FALSE(isTrustedModuleName("libmediainfo.0.dylib", true));
    CHECK(moduleCandidates("libmediainfo.0.dylib", true) ==
          Names{"@executable_path/libmediainfo.0.dylib", "/opt/homebrew/lib/libmediainfo.0.dylib",
                "/usr/local/lib/libmediainfo.0.dylib", "/usr/lib/libmediainfo.0.dylib"});
    CHECK(isTrustedModuleName("/opt/homebrew/lib/libmediainfo.0.dylib", true));
    CHECK(isTrustedModuleName("@executable_path/libmediainfo.0.dylib", true));
    CHECK(moduleCandidates("/opt/homebrew/lib/libmediainfo.0.dylib", true) ==
          Names{"/opt/homebrew/lib/libmediainfo.0.dylib"});
    // Every candidate is absolute or under @executable_path/.
    for (const std::string& c : moduleCandidates("libmediainfo.0.dylib", true))
        CHECK(isTrustedModuleName(c.c_str(), true));
}

namespace {
// Writes backslashes as '|' so the cases stay readable.
std::string bs(std::string s) {
    for (char& c : s)
        if (c == '|') c = '\\';
    return s;
}
std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool trusted(const std::string& s) {
    const std::string n = bs(s);
    const bool narrow = isTrustedWindowsModuleName(std::string_view(n));
    const std::wstring w = wide(n);
    CHECK(narrow == isTrustedWindowsModuleName(std::wstring_view(w)));
    return narrow;
}
}  // namespace

TEST_CASE("Windows module names: bare names and fully qualified paths only") {
    CHECK(trusted("MediaInfo.dll"));
    CHECK(trusted("C:|Tools|MediaInfo.dll"));
    CHECK(trusted("c:/Tools/MediaInfo.dll"));
    CHECK(trusted("||server|share|MediaInfo.dll"));
    CHECK(trusted("||?|C:|Tools|MediaInfo.dll"));

    CHECK_FALSE(trusted(".|MediaInfo.dll"));
    CHECK_FALSE(trusted("..|MediaInfo.dll"));
    CHECK_FALSE(trusted("lib|MediaInfo.dll"));
    CHECK_FALSE(trusted("./MediaInfo.dll"));
    CHECK_FALSE(trusted("C:MediaInfo.dll"));  // relative to the current directory on C:
    CHECK_FALSE(trusted("C:lib|MediaInfo.dll"));
    CHECK_FALSE(trusted(""));
}

#ifndef _WIN32
TEST_CASE("loadModule refuses a relative path without calling dlopen") {
    CHECK(loadModule("./libmediainfo.so.0", 0x1) == nullptr);
    CHECK(loadModule("sub/libmediainfo.so.0", 0x1) == nullptr);
}
#endif
