#include <cstdlib>

#include "doctest.h"
#include "fake_source.h"
#include "file_track.h"
#include "fsys.h"

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
