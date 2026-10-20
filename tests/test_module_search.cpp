// The runtime loader must never reach into the current directory
// (issue #29): the vendored header's "./libmediainfo.so.0" fallback is
// refused by the guard that mediainfo_source.cpp installs.
#include "doctest.h"
#include "module_search.h"

using namespace mediagrep;

#ifndef _WIN32

TEST_CASE("module names that depend on the current directory are refused") {
    CHECK_FALSE(isTrustedModuleName("./libmediainfo.so.0"));
    CHECK_FALSE(isTrustedModuleName("../libmediainfo.so.0"));
    CHECK_FALSE(isTrustedModuleName("lib/libmediainfo.so.0"));
    CHECK_FALSE(isTrustedModuleName("@loader_path/libmediainfo.0.dylib"));
    CHECK_FALSE(isTrustedModuleName(""));
    CHECK_FALSE(isTrustedModuleName(nullptr));
}

TEST_CASE("sonames, absolute paths and the executable directory are accepted") {
    CHECK(isTrustedModuleName("libmediainfo.so.0"));
    CHECK(isTrustedModuleName("libmediainfo.0.dylib"));
    CHECK(isTrustedModuleName("/usr/lib/libmediainfo.so.0"));
    CHECK(isTrustedModuleName("/usr/local/lib64/libmediainfo.so.0"));
    CHECK(isTrustedModuleName("@executable_path/libmediainfo.0.dylib"));
    CHECK_FALSE(isTrustedModuleName("@executable_pathx/libmediainfo.0.dylib"));
}

TEST_CASE("loadModule refuses a relative path without calling dlopen") {
    CHECK(loadModule("./libmediainfo.so.0", 0x1) == nullptr);
    CHECK(loadModule("sub/libmediainfo.so.0", 0x1) == nullptr);
}

#endif
