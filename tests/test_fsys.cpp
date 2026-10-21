#include <algorithm>
#include <atomic>
#include <cerrno>
#include <thread>
#include <vector>

#include "doctest.h"
#include "fsys.h"

using namespace mediagrep;
using fsys::FileType;

namespace {
const std::string kFixtures = MEDIAGREP_FIXTURES_DIR;
}

TEST_CASE("join adds exactly one separator") {
    const std::string j = fsys::join("a", "b");
    CHECK((j == "a/b" || j == "a\\b"));
    CHECK(fsys::join("a/", "b") == "a/b");
    CHECK(fsys::join("", "b") == "b");
#ifdef _WIN32
    CHECK(fsys::join("a\\", "b") == "a\\b");
#endif
}

TEST_CASE("fileName is the last component") {
    CHECK(fsys::fileName("a/b/c.mkv") == "c.mkv");
    CHECK(fsys::fileName("c.mkv") == "c.mkv");
    CHECK(fsys::fileName("a/b/") == "");
#ifdef _WIN32
    CHECK(fsys::fileName("a\\b\\c.mkv") == "c.mkv");
    CHECK(fsys::fileName("C:\\x.mkv") == "x.mkv");
#endif
}

TEST_CASE("status distinguishes files, directories and missing paths") {
    CHECK(fsys::status(kFixtures) == FileType::Directory);
    CHECK(fsys::status(fsys::join(kFixtures, "talk.mkv")) == FileType::Regular);
    std::string error = "unset";
    CHECK(fsys::status(fsys::join(kFixtures, "nope.mkv"), &error) == FileType::Missing);
    CHECK(error == "unset");  // a missing file is not an error
}

TEST_CASE("listDirectory returns entry names without . and ..") {
    std::vector<std::string> names;
    REQUIRE(fsys::listDirectory(kFixtures, names));
    CHECK(std::find(names.begin(), names.end(), "talk.mkv") != names.end());
    CHECK(std::find(names.begin(), names.end(), "make.sh") != names.end());
    CHECK(std::find(names.begin(), names.end(), ".") == names.end());
    CHECK(std::find(names.begin(), names.end(), "..") == names.end());
}

TEST_CASE("listDirectory fails with a message on a missing directory") {
    std::vector<std::string> names;
    std::string error;
    CHECK_FALSE(fsys::listDirectory(fsys::join(kFixtures, "nope"), names, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("regular files are not symlinks") {
    CHECK_FALSE(fsys::isSymlink(fsys::join(kFixtures, "talk.mkv")));
}

TEST_CASE("stat reports size and modification time") {
    auto info = fsys::stat(fsys::join(kFixtures, "talk.mkv"));
    REQUIRE(info);
    CHECK(info->size > 10000);
    CHECK(info->size < 20000);
    REQUIRE(info->modified);
    CHECK(*info->modified > 1700000000);  // after late 2023
#ifndef _WIN32
    CHECK(info->mode != 0);
    CHECK_FALSE(info->owner.empty());
#endif
}

TEST_CASE("stat on a missing path yields nothing") {
    CHECK_FALSE(fsys::stat(fsys::join(kFixtures, "nope.mkv")));
}

TEST_CASE("errorMessage describes an errno value") {
    const std::string enoent = fsys::errorMessage(ENOENT);
    CHECK_FALSE(enoent.empty());
    CHECK(enoent.find("such file") != std::string::npos);  // "No such" / "no such"
    CHECK_FALSE(fsys::errorMessage(999999).empty());  // unknown values still get text
}

// Run under ThreadSanitizer (the Analysis workflow's TSan job): the worker
// threads call stat() concurrently, and its owner/group lookups must not
// share a static buffer.
TEST_CASE("stat and errorMessage are safe to call from several threads") {
    const std::string path = fsys::join(kFixtures, "talk.mkv");
    const auto expected = fsys::stat(path);
    REQUIRE(expected);
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 200; ++i) {
                const auto info = fsys::stat(path);
                if (!info || info->owner != expected->owner || info->group != expected->group)
                    ++mismatches;
                if (fsys::errorMessage(ENOENT).empty()) ++mismatches;
            }
        });
    for (std::thread& th : threads) th.join();
    CHECK(mismatches == 0);
}
