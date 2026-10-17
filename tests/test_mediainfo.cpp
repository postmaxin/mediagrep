// Integration tests against MediaInfoLib and the committed fixtures.
#include <cstdlib>
#include <filesystem>

#include "doctest.h"
#include "eval.h"
#include "mediainfo_source.h"
#include "parser.h"

using namespace mediagrep;

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MEDIAGREP_FIXTURES_DIR) / name;
}

std::string value(const MediaInfoSource& src, TrackKind kind, std::size_t index, const char* name) {
    auto fv = src.lookup(kind, index, name);
    return fv ? fv->value : std::string("<missing>");
}

}  // namespace

TEST_CASE("the library can be loaded and reports a version") {
    auto v = MediaInfoSource::libraryVersion();
    REQUIRE(v.has_value());
    CHECK(v->find("MediaInfoLib") != std::string::npos);
}

TEST_CASE("opening a missing file fails") {
    MediaInfoSource src;
    CHECK_FALSE(src.open(fixture("does-not-exist.mkv")));
    CHECK_FALSE(src.isMedia());
}

TEST_CASE("a text file opens but is not media") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("notes.txt")));
    CHECK_FALSE(src.isMedia());
}

TEST_CASE("talk.mkv: track layout") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    CHECK(src.isMedia());
    CHECK(src.trackCount(TrackKind::General) == 1);
    CHECK(src.trackCount(TrackKind::Video) == 1);
    CHECK(src.trackCount(TrackKind::Audio) == 2);
    CHECK(src.trackCount(TrackKind::Text) == 0);
    CHECK(src.trackCount(TrackKind::Menu) == 0);
}

TEST_CASE("talk.mkv: general tags, looked up case-insensitively with canonical names") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    CHECK(value(src, TrackKind::General, 0, "Format") == "Matroska");
    CHECK(value(src, TrackKind::General, 0, "title") == "Talk about Engines");
    CHECK(value(src, TrackKind::General, 0, "PUBLISHER") == "Interesting Engineering");
    auto fv = src.lookup(TrackKind::General, 0, "title");
    REQUIRE(fv);
    CHECK(fv->name == "Title");
    CHECK(fv->type == ValueType::Text);
}

TEST_CASE("talk.mkv: numeric fields carry type and measure, values are raw") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    auto dur = src.lookup(TrackKind::General, 0, "Duration");
    REQUIRE(dur);
    CHECK(dur->type == ValueType::Float);
    CHECK(dur->measure == " ms");
    const double ms = std::strtod(dur->value.c_str(), nullptr);
    CHECK(ms > 1900);
    CHECK(ms < 2100);

    auto h = src.lookup(TrackKind::Video, 0, "Height");
    REQUIRE(h);
    CHECK(h->value == "32");
    CHECK(h->type == ValueType::Integer);
    CHECK(h->measure == " pixel");

    auto size = src.lookup(TrackKind::General, 0, "FileSize");
    REQUIRE(size);
    // MediaInfoLib declares FileSize as text; the measure is what marks it numeric.
    CHECK(size->measure == " byte");
}

TEST_CASE("talk.mkv: per-track fields") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    CHECK(value(src, TrackKind::Audio, 0, "Language") == "en");
    CHECK(value(src, TrackKind::Audio, 1, "Language") == "ja");
    CHECK(value(src, TrackKind::Video, 0, "Format") == "AVC");
    CHECK(value(src, TrackKind::Audio, 2, "Language") == "<missing>");
}

TEST_CASE("forEachField enumerates only fields with values") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    bool sawFormat = false, sawEmpty = false;
    std::size_t count = 0;
    src.forEachField(TrackKind::General, 0, [&](const FieldValue& fv) {
        ++count;
        if (fv.name == "Format") sawFormat = true;
        if (fv.value.empty()) sawEmpty = true;
    });
    CHECK(sawFormat);
    CHECK_FALSE(sawEmpty);
    CHECK(count > 10);
}

TEST_CASE("clip.mp4 and song.mp3") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("clip.mp4")));
    CHECK(value(src, TrackKind::General, 0, "Format") == "MPEG-4");
    CHECK(value(src, TrackKind::Video, 0, "Height") == "720");
    CHECK(value(src, TrackKind::General, 0, "Title") == "Sixty Minutes of Nothing");

    REQUIRE(src.open(fixture("song.mp3")));
    CHECK(src.trackCount(TrackKind::Video) == 0);
    CHECK(src.trackCount(TrackKind::Audio) == 1);
    CHECK(value(src, TrackKind::General, 0, "Title") == "Quiet Song");
    CHECK(value(src, TrackKind::General, 0, "Album") == "Silence");
    CHECK(value(src, TrackKind::General, 0, "Performer") == "Nobody");
    CHECK(value(src, TrackKind::General, 0, "Recorded_Date") == "2019");
}

TEST_CASE("reopening replaces the previous file's metadata") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    REQUIRE(src.open(fixture("song.mp3")));
    CHECK(src.trackCount(TrackKind::Audio) == 1);
    CHECK(value(src, TrackKind::General, 0, "Publisher") == "<missing>");
}

TEST_CASE("end to end: evaluator over the real backend") {
    MediaInfoSource src;
    REQUIRE(src.open(fixture("talk.mkv")));
    ExprPtr expr = parse("Audio/Language = ja and Video/Height < 1000 and 'Engineering'");
    Match m = Evaluator(*expr, EvalOptions{}).evaluate(src);
    REQUIRE(m.matched);
    REQUIRE(m.fields.size() == 3);
    CHECK(m.fields[0].location() == "Audio[2]/Language");
    CHECK(m.fields[1].location() == "Video/Height");
    CHECK(m.fields[2].location() == "General/PUBLISHER");  // MediaInfo keeps Matroska tag names as-is
}
