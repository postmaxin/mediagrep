// The test makes its own 5 GiB file, so it needs a 64-bit off_t whatever the build says;
// the library under test has to get one from the build.
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <thread>
#include <vector>

#include "doctest.h"
#include "fsys.h"
#include "tmp_tree.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

TEST_CASE("collectEntries stops cleanly at the end and drops . and ..") {
    std::vector<std::string> in{".", "a", "..", "b"};
    std::size_t i = 0;
    std::vector<std::string> names;
    std::string error = "unset";
    CHECK(fsys::detail::collectEntries(
        [&](int& err) -> std::optional<std::string> {
            err = 0;
            if (i == in.size()) return std::nullopt;
            return in[i++];
        },
        names, &error));
    CHECK(names == std::vector<std::string>{"a", "b"});
    CHECK(error == "unset");
}

TEST_CASE("collectEntries reports a read error part-way as a failure, not the end") {
    std::size_t i = 0;
    std::vector<std::string> names;
    std::string error;
    CHECK_FALSE(fsys::detail::collectEntries(
        [&](int& err) -> std::optional<std::string> {
            err = 0;
            if (i++ == 2) { err = 5; return std::nullopt; }  // EIO on POSIX; any nonzero code works
            return std::string(1, static_cast<char>('a' + i));
        },
        names, &error));
    CHECK_FALSE(error.empty());
    CHECK(names.size() == 2);  // what was read before the failure
}

TEST_CASE("collectEntries without an error pointer still reports failure") {
    std::vector<std::string> names;
    CHECK_FALSE(fsys::detail::collectEntries(
        [](int& err) -> std::optional<std::string> { err = 5; return std::nullopt; }, names));
}

TEST_CASE("only 'no more files' ends a Windows listing, and an empty root is not an error") {
    constexpr unsigned long kNoMoreFiles = 18, kFileNotFound = 2, kNetworkNameDeleted = 64;
    CHECK(fsys::detail::findFailureEndsListing(kNoMoreFiles, false));
    CHECK_FALSE(fsys::detail::findFailureEndsListing(kNetworkNameDeleted, false));
    CHECK_FALSE(fsys::detail::findFailureEndsListing(kFileNotFound, false));
    // FindFirstFileW on an empty drive root (no . or ..) fails with FILE_NOT_FOUND.
    CHECK(fsys::detail::findFailureEndsListing(kFileNotFound, true));
    CHECK_FALSE(fsys::detail::findFailureEndsListing(kNetworkNameDeleted, true));
}

TEST_CASE("FILETIME ticks convert to Unix time, before 1970 as well") {
    constexpr std::uint64_t kEpoch = 116444736000000000ULL;  // 1970-01-01 in 100 ns ticks
    CHECK_FALSE(fsys::detail::fileTimeToUnix(0));
    CHECK(*fsys::detail::fileTimeToUnix(kEpoch) == 0);
    CHECK(*fsys::detail::fileTimeToUnix(kEpoch + 86400ULL * 10000000ULL) == 86400);
    CHECK(*fsys::detail::fileTimeToUnix(kEpoch - 10000000ULL) == -1);
    CHECK(*fsys::detail::fileTimeToUnix(kEpoch - 1) == -1);  // rounds down, not toward zero
    CHECK(*fsys::detail::fileTimeToUnix(kEpoch - 86400ULL * 365 * 10000000ULL) == -86400 * 365);
    // 1601 is far in the past: negative where time_t can hold it, unknown where
    // a 32-bit time_t cannot (it would otherwise wrap to a date in 2009).
    if (sizeof(std::time_t) >= 8)
        CHECK(*fsys::detail::fileTimeToUnix(1) < 0);
    else
        CHECK_FALSE(fsys::detail::fileTimeToUnix(1));
}

TEST_CASE("only name-surrogate reparse tags are links") {
    using fsys::detail::isLinkReparseTag;
    CHECK(isLinkReparseTag(0xA000000C));        // IO_REPARSE_TAG_SYMLINK
    CHECK(isLinkReparseTag(0xA0000003));        // IO_REPARSE_TAG_MOUNT_POINT (junction)
    CHECK(isLinkReparseTag(0xA000001D));        // IO_REPARSE_TAG_LX_SYMLINK (WSL)
    CHECK_FALSE(isLinkReparseTag(0x9000601A));  // OneDrive Files On-Demand (CLOUD_6)
    CHECK_FALSE(isLinkReparseTag(0x9000001A));  // IO_REPARSE_TAG_CLOUD
    CHECK_FALSE(isLinkReparseTag(0x9000F01A));  // IO_REPARSE_TAG_CLOUD_F
    CHECK_FALSE(isLinkReparseTag(0x80000013));  // IO_REPARSE_TAG_DEDUP
    CHECK_FALSE(isLinkReparseTag(0x8000001B));  // IO_REPARSE_TAG_APPEXECLINK
    CHECK_FALSE(isLinkReparseTag(0x80000023));  // IO_REPARSE_TAG_AF_UNIX
    CHECK_FALSE(isLinkReparseTag(0));           // not a reparse point
}

TEST_CASE("windowsJoin adds no separator after a bare drive letter") {
    using fsys::detail::windowsJoin;
    CHECK(windowsJoin("C:", "x.mkv") == "C:x.mkv");  // the current directory on drive C
    CHECK(windowsJoin("c:", "x.mkv") == "c:x.mkv");
    CHECK(windowsJoin("C:\\", "x.mkv") == "C:\\x.mkv");
    CHECK(windowsJoin("C:/", "x.mkv") == "C:/x.mkv");
    CHECK(windowsJoin("C:\\a", "x.mkv") == "C:\\a\\x.mkv");
    CHECK(windowsJoin("a", "x.mkv") == "a\\x.mkv");
    CHECK(windowsJoin("", "x.mkv") == "x.mkv");
    CHECK(windowsJoin("CC:", "x.mkv") == "CC:\\x.mkv");  // not a drive
    CHECK(windowsJoin("1:", "x.mkv") == "1:\\x.mkv");
}

TEST_CASE("windowsSearchPattern lists 'C:' as the current directory on that drive") {
    using fsys::detail::windowsSearchPattern;
    CHECK(windowsSearchPattern("C:") == "C:*");  // C:\* would be the drive root
    CHECK(windowsSearchPattern("C:\\") == "C:\\*");
    CHECK(windowsSearchPattern("C:\\a") == "C:\\a\\*");
    CHECK(windowsSearchPattern("a/") == "a/*");
    CHECK(windowsSearchPattern("") == "*");
    CHECK(windowsSearchPattern("\\\\server\\share") == "\\\\server\\share\\*");
}

#ifndef _WIN32
// A 32-bit build without _FILE_OFFSET_BITS=64 gets EOVERFLOW from stat() on a file of
// 2 GiB or more, which is how a 4 GiB MKV on a 32-bit NAS went missing.
TEST_CASE("stat and status handle a sparse file over 4 GiB") {
    mediagrep::test::TempTree t{"mediagrep-big"};
    const std::string path = t.path("big.mkv");
    constexpr std::uint64_t kSize = 5ULL << 30;

    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY, 0600);
    REQUIRE(fd >= 0);
    bool sparse = ::ftruncate(fd, static_cast<off_t>(kSize)) == 0;
    struct stat st;
    // A file system without sparse files would write out 5 GiB of zeros.
    if (sparse && ::fstat(fd, &st) == 0)
        sparse = static_cast<std::uint64_t>(st.st_blocks) * 512 < (1ULL << 30);
    ::close(fd);
    if (!sparse) {
        MESSAGE("skipped: this file system has no sparse files");
        return;
    }

    auto info = fsys::stat(path);
    REQUIRE(info);
    CHECK(info->size == kSize);
    CHECK(fsys::status(path) == FileType::Regular);
    std::vector<std::string> names;
    REQUIRE(fsys::listDirectory(t.root, names));
    CHECK(names == std::vector<std::string>{"big.mkv"});
}
#endif
