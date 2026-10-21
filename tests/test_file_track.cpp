#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "doctest.h"
#include "fake_source.h"
#include "file_track.h"
#include "fsys.h"
#include "tmp_tree.h"

using namespace mediagrep;
using namespace mediagrep::test;

namespace {
const std::string kFixtures = MEDIAGREP_FIXTURES_DIR;
std::string fixture(const char* name) { return fsys::join(kFixtures, name); }
}  // namespace

TEST_CASE("the File track is added on top of the inner source") {
    FakeSource inner;
    inner.add(TrackKind::General, {text("Format", "Matroska")});
    FileTrackSource src(inner, fixture("talk.mkv"));

    CHECK(src.trackCount(TrackKind::General) == 1);
    CHECK(src.trackCount(TrackKind::Video) == 0);
    CHECK(src.trackCount(TrackKind::File) == 1);
    REQUIRE(src.lookup(TrackKind::General, 0, "Format"));
    CHECK(src.lookup(TrackKind::General, 0, "Format")->value == "Matroska");
}

TEST_CASE("File track: name, directory, extension") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    auto name = src.lookup(TrackKind::File, 0, "name");  // case-insensitive
    REQUIRE(name);
    CHECK(name->name == "Name");
    CHECK(name->value == "talk.mkv");
    CHECK(name->type == ValueType::Text);
    CHECK(src.lookup(TrackKind::File, 0, "Extension")->value == "mkv");
    CHECK(src.lookup(TrackKind::File, 0, "Directory")->value == kFixtures);
}

TEST_CASE("File track: size is an integer in bytes") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    auto size = src.lookup(TrackKind::File, 0, "Size");
    REQUIRE(size);
    CHECK(size->type == ValueType::Integer);
    CHECK(size->measure == " byte");
    const unsigned long bytes = std::strtoul(size->value.c_str(), nullptr, 10);
    CHECK(bytes > 10000);
    CHECK(bytes < 20000);
}

TEST_CASE("File track: times are UTC date strings") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    auto mod = src.lookup(TrackKind::File, 0, "Modified");
    REQUIRE(mod);
    CHECK(mod->value.size() == 23);
    CHECK(mod->value.substr(19) == " UTC");
    CHECK(mod->value.substr(0, 2) == "20");
}

TEST_CASE("File track: forEachField lists every field") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    int count = 0;
    bool sawName = false;
    src.forEachField(TrackKind::File, 0, [&](const FieldValue& fv) {
        ++count;
        if (fv.name == "Name") sawName = true;
    });
    CHECK(sawName);
    CHECK(count >= 5);
    // Index 1 does not exist.
    int none = 0;
    src.forEachField(TrackKind::File, 1, [&](const FieldValue&) { ++none; });
    CHECK(none == 0);
}

#ifndef _WIN32
TEST_CASE("File track: mode, owner and group on POSIX") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    auto mode = src.lookup(TrackKind::File, 0, "Mode");
    REQUIRE(mode);
    CHECK(mode->value.size() == 4);
    CHECK(mode->value[0] == '0');
    REQUIRE(src.lookup(TrackKind::File, 0, "Owner"));
    CHECK_FALSE(src.lookup(TrackKind::File, 0, "Owner")->value.empty());
}
#endif

TEST_CASE("a file that cannot be examined has no File track") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("nope.mkv"));
    CHECK(src.trackCount(TrackKind::File) == 0);
    CHECK_FALSE(src.lookup(TrackKind::File, 0, "Name"));
}

TEST_CASE("the Path track comes from the file name, even when stat fails") {
    FakeSource inner;
    FileTrackSource src(inner, fsys::join(fsys::join(kFixtures, "Season 2"),
                                          "Show - S02E05 - Title 2024-03-01 [WcHWQnoE95w].mkv"));
    CHECK(src.trackCount(TrackKind::File) == 0);
    REQUIRE(src.trackCount(TrackKind::Path) == 1);
    CHECK(src.lookup(TrackKind::Path, 0, "when")->value == "2024-03-01");
    CHECK(src.lookup(TrackKind::Path, 0, "Season")->value == "2");
    CHECK(src.lookup(TrackKind::Path, 0, "Episode")->value == "5");
    CHECK(src.lookup(TrackKind::Path, 0, "Id")->value == "WcHWQnoE95w");
    int count = 0;
    src.forEachField(TrackKind::Path, 0, [&](const FieldValue&) { ++count; });
    CHECK(count == 4);
}

TEST_CASE("a name with nothing recognizable has no Path track") {
    FakeSource inner;
    FileTrackSource src(inner, fixture("talk.mkv"));
    CHECK(src.trackCount(TrackKind::Path) == 0);
}

namespace {
// Writes backslashes as '|' so the cases below stay readable.
std::string bs(std::string s) {
    for (char& c : s)
        if (c == '|') c = '\\';
    return s;
}
}  // namespace

TEST_CASE("the directory of a file at a Windows drive root keeps its separator") {
    CHECK(directoryOfPath(bs("E:|clip.mkv"), true) == bs("E:|"));
    CHECK(directoryOfPath("E:/clip.mkv", true) == "E:/");
    CHECK(directoryOfPath(bs("||?|E:|clip.mkv"), true) == bs("||?|E:|"));
    CHECK(directoryOfPath(bs("E:|a|clip.mkv"), true) == bs("E:|a"));
    CHECK(directoryOfPath(bs("E:|a||clip.mkv"), true) == bs("E:|a"));
    CHECK(directoryOfPath(bs("||srv|share|x.mkv"), true) == bs("||srv|share"));
    CHECK(directoryOfPath("clip.mkv", true) == ".");
    // "E:clip.mkv" is relative to the current directory on E:, not the root.
    CHECK(directoryOfPath("E:clip.mkv", true) == "E:");
}

TEST_CASE("the directory of a POSIX path") {
    CHECK(directoryOfPath("talk.mkv", false) == ".");
    CHECK(directoryOfPath("/a/b.mkv", false) == "/a");
    CHECK(directoryOfPath("/b.mkv", false) == "/");
    CHECK(directoryOfPath("a//b.mkv", false) == "a");
    CHECK(directoryOfPath("./b.mkv", false) == ".");
    CHECK(directoryOfPath("../b.mkv", false) == "..");
    // A backslash is an ordinary character in a POSIX name.
    CHECK(directoryOfPath(bs("E:|clip.mkv"), false) == ".");
}

namespace {

// Runs in `dir` and returns to the previous working directory.
struct InDirectory {
    std::string saved;
    explicit InDirectory(const std::string& dir) {
        char buf[4096];
#ifdef _WIN32
        saved = _getcwd(buf, sizeof buf) ? buf : ".";
        REQUIRE(_chdir(dir.c_str()) == 0);
#else
        saved = getcwd(buf, sizeof buf) ? buf : ".";
        REQUIRE(::chdir(dir.c_str()) == 0);
#endif
    }
    ~InDirectory() {
#ifdef _WIN32
        _chdir(saved.c_str());
#else
        if (::chdir(saved.c_str()) != 0) std::abort();
#endif
    }
};

}  // namespace

TEST_CASE("a bare name takes its Path season from the real parent directory") {
    TempTree tree("mg-filetrack");
    tree.file("Season 2/Episode 3.mkv");
    tree.dir("Season 2/sub");
    FakeSource inner;

    // From the parent: the displayed directory has the right name already.
    FileTrackSource viaParent(inner, tree.path("Season 2/Episode 3.mkv"));
    CHECK(viaParent.lookup(TrackKind::Path, 0, "Season")->value == "2");

    InDirectory here(tree.path("Season 2"));
    for (const char* path : {"Episode 3.mkv", "./Episode 3.mkv"}) {
        CAPTURE(path);
        FileTrackSource src(inner, path);
        REQUIRE(src.lookup(TrackKind::Path, 0, "Season"));
        CHECK(src.lookup(TrackKind::Path, 0, "Season")->value == "2");
        // File/Directory stays as the user gave it.
        REQUIRE(src.lookup(TrackKind::File, 0, "Directory"));
        CHECK(src.lookup(TrackKind::File, 0, "Directory")->value == ".");
    }
    CHECK(recognitionDirectoryName(".") == "Season 2");
    CHECK(recognitionDirectoryName("sub/..") == "Season 2");
    CHECK(recognitionDirectoryName("sub") == "sub");  // not resolved, not special
    {
        InDirectory deeper(tree.path("Season 2/sub"));
        FileTrackSource src(inner, "../Episode 3.mkv");
        REQUIRE(src.lookup(TrackKind::Path, 0, "Season"));
        CHECK(src.lookup(TrackKind::Path, 0, "Season")->value == "2");
        CHECK(src.lookup(TrackKind::File, 0, "Directory")->value == "..");
    }
}

#ifndef _WIN32
TEST_CASE("File/Mode is present even when the permission bits are 0000") {
    TempTree tree("mg-filemode");
    tree.file("locked.mkv");
    const std::string path = tree.path("locked.mkv");
    REQUIRE(::chmod(path.c_str(), 0) == 0);
    FakeSource inner;
    FileTrackSource src(inner, path);
    ::chmod(path.c_str(), 0600);
    const auto mode = src.lookup(TrackKind::File, 0, "Mode");
    REQUIRE(mode);
    CHECK(mode->value == "0000");
}
#endif
