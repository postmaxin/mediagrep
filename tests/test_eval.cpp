#include "doctest.h"
#include "eval.h"
#include "fake_source.h"
#include "parser.h"

using namespace mediagrep;
using namespace mediagrep::test;

namespace {

Match run(std::string_view text, const MetadataSource& src, bool ignoreCase = false) {
    ExprPtr expr = parse(text);
    Evaluator ev(*expr, EvalOptions{ignoreCase});
    return ev.evaluate(src);
}

bool matches(std::string_view text, const MetadataSource& src, bool ignoreCase = false) {
    return run(text, src, ignoreCase).matched;
}

std::vector<std::string> fields(const Match& m) {
    std::vector<std::string> out;
    for (const auto& f : m.fields) out.push_back(f.location() + ": " + f.value);
    return out;
}

// A typical video: one General, one Video, two Audio (en, ja), one Text.
FakeSource typicalVideo() {
    FakeSource s;
    s.add(TrackKind::General, {
        text("Format", "Matroska"),
        text("Title", "Talk about Engines"),
        text("Publisher", "Interesting Engineering"),
        flt("Duration", "5400000.000000", " ms"),
        integer("FileSize", "734003200", " byte"),
        flt("OverallBitRate", "1087412.000", " bps"),
        text("Encoded_Date", "UTC 2024-03-01 12:00:00"),
        binary("Cover_Data", "AAAA"),
    });
    s.add(TrackKind::Video, {
        text("Format", "AVC"),
        integer("Width", "1280", " pixel"),
        integer("Height", "720", " pixel"),
        text("Title", "Talk about Engines"),
    });
    s.add(TrackKind::Audio, {text("Format", "AAC"), text("Language", "en")});
    s.add(TrackKind::Audio, {text("Format", "AAC"), text("Language", "ja")});
    s.add(TrackKind::Text, {text("Format", "UTF-8"), text("Language", "en")});
    return s;
}

}  // namespace

TEST_CASE("text equality matches the exact value and reports the field") {
    auto src = typicalVideo();
    Match m = run("General/Format = Matroska", src);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"General/Format: Matroska"});
    CHECK_FALSE(matches("General/Format = MP4", src));
}

TEST_CASE("text equality is case-sensitive unless ignoreCase") {
    auto src = typicalVideo();
    CHECK_FALSE(matches("General/Format = matroska", src));
    CHECK(matches("General/Format = matroska", src, true));
}

TEST_CASE("inequality is false when the field is missing") {
    auto src = typicalVideo();
    CHECK(matches("General/Format != MP4", src));
    CHECK_FALSE(matches("General/Format != Matroska", src));
    CHECK_FALSE(matches("General/Nope != MP4", src));
}

TEST_CASE("regex match is unanchored and honours ignoreCase") {
    auto src = typicalVideo();
    CHECK(matches("Publisher ~ Engineering", src));
    CHECK(matches("Publisher ~ ^Interesting", src));
    CHECK_FALSE(matches("Publisher ~ engineering", src));
    CHECK(matches("Publisher ~ engineering", src, true));
    CHECK(matches("Publisher ~ 'Eng.*ing$'", src));
}

TEST_CASE("negated regex is true only when the field exists and does not match") {
    auto src = typicalVideo();
    CHECK(matches("Publisher !~ Boring", src));
    CHECK_FALSE(matches("Publisher !~ Engineering", src));
    CHECK_FALSE(matches("Nope !~ Boring", src));
}

TEST_CASE("numeric fields compare numerically, not lexically") {
    auto src = typicalVideo();
    CHECK(matches("Video/Height < 1000", src));   // "720" < "1000" is false lexically
    CHECK(matches("Video/Height >= 720", src));
    CHECK(matches("Video/Height = 720", src));
    CHECK_FALSE(matches("Video/Height > 720", src));
    CHECK(matches("Video/Height != 1080", src));
}

TEST_CASE("duration operands use time units against millisecond values") {
    auto src = typicalVideo();
    CHECK(matches("Duration > 1h", src));
    CHECK(matches("Duration = 90m", src));
    CHECK(matches("Duration = 1h30m", src));
    CHECK(matches("Duration <= 5400", src));
    CHECK_FALSE(matches("Duration > 2h", src));
}

TEST_CASE("size and bitrate operands use decimal multipliers") {
    auto src = typicalVideo();
    CHECK(matches("FileSize > 700M", src));
    CHECK(matches("FileSize < 1G", src));
    CHECK(matches("OverallBitRate > 1M", src));
    CHECK(matches("OverallBitRate < 1.1Mb", src));
}

TEST_CASE("a text-typed field with a measure and a numeric value compares numerically") {
    // MediaInfoLib declares some numeric parameters (FileSize) as text.
    FakeSource withMeasure;
    withMeasure.add(TrackKind::General,
                    {FieldValue{"FileSize", "734003200", ValueType::Text, " byte"}});
    CHECK(matches("FileSize > 700M", withMeasure));
    CHECK(matches("FileSize < 1G", withMeasure));
    CHECK_FALSE(matches("FileSize > 1G", withMeasure));

    // Without a measure, text stays text: "734003200" < "9" lexically.
    FakeSource plainText;
    plainText.add(TrackKind::General, {text("FileSize", "734003200")});
    CHECK(matches("FileSize < 9", plainText));
}

TEST_CASE("a non-numeric operand on a numeric field is false with a warning") {
    auto src = typicalVideo();
    Match m = run("Video/Height < tall", src);
    CHECK_FALSE(m.matched);
    REQUIRE(m.warnings.size() == 1);
    CHECK(m.warnings[0].find("Height") != std::string::npos);
    CHECK(m.warnings[0].find("tall") != std::string::npos);
}

TEST_CASE("a comparison is true if any in-scope track satisfies it") {
    auto src = typicalVideo();
    Match m = run("Audio/Language = ja", src);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"Audio[2]/Language: ja"});
    CHECK_FALSE(matches("Video/Language = ja", src));
}

TEST_CASE("a track index pins one track, 1-based") {
    auto src = typicalVideo();
    CHECK_FALSE(matches("Audio[1]/Language = ja", src));
    CHECK(matches("Audio[2]/Language = ja", src));
    CHECK_FALSE(matches("Audio[3]/Language = ja", src));
}

TEST_CASE("an unscoped field is searched in every track of every kind") {
    auto src = typicalVideo();
    CHECK(matches("Language = ja", src));
    Match m = run("Title = 'Talk about Engines'", src);
    CHECK(fields(m) == std::vector<std::string>{
        "General/Title: Talk about Engines", "Video/Title: Talk about Engines"});
}

TEST_CASE("all satisfying tracks are reported") {
    auto src = typicalVideo();
    Match m = run("Audio/Format = AAC", src);
    CHECK(fields(m) == std::vector<std::string>{"Audio[1]/Format: AAC", "Audio[2]/Format: AAC"});
}

TEST_CASE("single-track kinds print without an index") {
    auto src = typicalVideo();
    Match m = run("Text/Language = en", src);
    CHECK(fields(m) == std::vector<std::string>{"Text/Language: en"});
}

TEST_CASE("not inverts and reports no fields") {
    auto src = typicalVideo();
    Match m = run("not Audio/Language = fr", src);
    CHECK(m.matched);
    CHECK(m.fields.empty());
    CHECK_FALSE(matches("not Audio/Language = ja", src));
}

TEST_CASE("and requires every operand and reports all their fields") {
    auto src = typicalVideo();
    Match m = run("Video/Height < 1000 and Audio/Language = ja", src);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"Video/Height: 720", "Audio[2]/Language: ja"});
    CHECK_FALSE(matches("Video/Height < 1000 and Audio/Language = fr", src));
}

TEST_CASE("or requires any operand and reports the fields of those that matched") {
    auto src = typicalVideo();
    Match m = run("Audio/Language = fr or Video/Height = 720 or Text/Language = en", src);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"Video/Height: 720", "Text/Language: en"});
    CHECK_FALSE(matches("Audio/Language = fr or Video/Height = 1080", src));
}

TEST_CASE("exists is true for a present, non-empty field") {
    FakeSource src;
    src.add(TrackKind::General, {text("Title", "x"), text("Empty", "")});
    CHECK(matches("Title", src));
    CHECK(matches("General/Title", src));
    CHECK_FALSE(matches("Empty", src));
    CHECK_FALSE(matches("Missing", src));
    Match m = run("Title", src);
    CHECK(fields(m) == std::vector<std::string>{"General/Title: x"});
}

TEST_CASE("a bare pattern searches every non-binary field in every track") {
    auto src = typicalVideo();
    Match m = run("'Engineering'", src);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"General/Publisher: Interesting Engineering"});
    CHECK(matches("'^ja$'", src));
    CHECK(matches("'720'", src));          // numeric fields are searched too
    CHECK_FALSE(matches("'AAAA'", src));   // binary fields are not
    CHECK_FALSE(matches("'engineering'", src));
    CHECK(matches("'engineering'", src, true));
}

TEST_CASE("a bare pattern ignores the path-derived fields the library synthesizes") {
    // MediaInfoLib reports CompleteName, FolderName, FileName and friends as
    // General fields. Matching those would make every search a path search.
    FakeSource src;
    src.add(TrackKind::General, {
        text("CompleteName", "/media/Johnny Harris/clip.mkv"),
        text("FolderName", "/media/Johnny Harris"),
        text("FileNameExtension", "clip.mkv"),
        text("FileName", "clip"),
        text("FileExtension", "mkv"),
        text("Title", "A clip"),
    });
    CHECK_FALSE(matches("'Johnny Harris'", src));
    CHECK_FALSE(matches("'mkv'", src, true));
    CHECK(matches("'A clip'", src));
    // Explicit comparisons on those fields still work.
    CHECK(matches("FolderName ~ 'Johnny'", src));
}

TEST_CASE("a bare pattern reports every field it matched") {
    auto src = typicalVideo();
    Match m = run("'AAC'", src);
    CHECK(fields(m) == std::vector<std::string>{"Audio[1]/Format: AAC", "Audio[2]/Format: AAC"});
}

TEST_CASE("date-shaped values compare chronologically at the operand's granularity") {
    auto src = typicalVideo();
    CHECK(matches("Encoded_Date > 2023", src));
    CHECK_FALSE(matches("Encoded_Date > 2024", src));
    CHECK(matches("Encoded_Date >= 2024", src));
    CHECK(matches("Encoded_Date = 2024", src));
    CHECK(matches("Encoded_Date = 2024-03", src));
    CHECK_FALSE(matches("Encoded_Date = 2024-04", src));
    CHECK(matches("Encoded_Date < '2024-03-01 13:00'", src));
    CHECK(matches("Encoded_Date != 2025", src));
}

TEST_CASE("a text field that merely starts with a date does not equal the bare date") {
    FakeSource src;
    src.add(TrackKind::General, {text("Title", "2024-03-01 Interview")});
    src.add(TrackKind::File, {text("Name", "2024-03-01.mkv")});
    CHECK_FALSE(matches("File/Name = 2024-03-01", src));
    CHECK_FALSE(matches("Title = 2024-03-01", src));
    CHECK(matches("Title != 2024-03-01", src));
    // Exact text still matches.
    CHECK(matches("File/Name = 2024-03-01.mkv", src));
    CHECK(matches("Title = '2024-03-01 Interview'", src));
    CHECK(matches("Title ~ 2024-03-01", src));
}

TEST_CASE("MediaInfo's date forms still compare by value") {
    FakeSource src;
    src.add(TrackKind::General, {text("A", "UTC 2024-03-01 12:34:56"),
                                 text("B", "2024-03-01T12:34:56Z"),
                                 text("C", "2024-03-01 12:34:56 UTC")});
    CHECK(matches("A = 2024-03-01", src));
    CHECK(matches("B = 2024-03", src));
    CHECK(matches("C >= 2024", src));
    CHECK(matches("B < '2024-03-01 13:00'", src));
}

TEST_CASE("a non-date operand against a date value falls back to text comparison") {
    auto src = typicalVideo();
    CHECK(matches("Encoded_Date ~ UTC", src));
    CHECK(matches("Encoded_Date = 'UTC 2024-03-01 12:00:00'", src));
}

TEST_CASE("text ordering is lexical") {
    auto src = typicalVideo();
    CHECK(matches("General/Format < N", src));
    CHECK_FALSE(matches("General/Format > N", src));
}

TEST_CASE("a field reported by two sub-expressions is listed once") {
    auto src = typicalVideo();
    Match m = run("Video/Height = 720 or Video/Height < 1000", src);
    CHECK(fields(m) == std::vector<std::string>{"Video/Height: 720"});
}

TEST_CASE("an invalid regex is rejected when the evaluator is built") {
    ExprPtr expr = parse("Title ~ '('");
    CHECK_THROWS_AS(Evaluator(*expr, EvalOptions{}), EvalError);
}

TEST_CASE("File fields are in scope for unscoped names, and bare patterns skip them") {
    FakeSource src;
    src.add(TrackKind::General, {text("Title", "Talk")});
    src.add(TrackKind::File, {integer("Size", "5000000000", " byte"), text("Name", "talk.mkv"),
                              text("Modified", "2024-03-01 12:00:00 UTC")});
    CHECK(matches("Size > 4G", src));
    CHECK(matches("File/Size > 4G", src));
    CHECK(matches("File/Modified >= 2024-03", src));
    CHECK_FALSE(matches("File/Modified >= 2024-04", src));
    CHECK_FALSE(matches("'talk'", src));
    CHECK(matches("File/Name ~ talk", src));
}

TEST_CASE("@when expands to every date-bearing field and names the real source") {
    FakeSource a;
    a.add(TrackKind::General, {text("Encoded_Date", "UTC 2024-03-01 12:00:00")});
    Match m = run("@when = 2024", a);
    CHECK(m.matched);
    CHECK(fields(m) == std::vector<std::string>{"General/Encoded_Date: UTC 2024-03-01 12:00:00"});

    FakeSource b;
    b.add(TrackKind::General, {text("DATE", "20240121")});
    b.add(TrackKind::File, {text("Modified", "2026-01-22 03:14:05 UTC")});
    CHECK(matches("@when = 2024", b));
    CHECK(matches("@when = 2026", b));
    CHECK_FALSE(matches("@when = 2025", b));
    CHECK(fields(run("@when >= 2024", b)) == std::vector<std::string>{
        "File/Modified: 2026-01-22 03:14:05 UTC", "General/DATE: 20240121"});
}

TEST_CASE("@who and @name") {
    auto src = typicalVideo();
    CHECK(fields(run("@who ~ Engineering", src)) ==
          std::vector<std::string>{"General/Publisher: Interesting Engineering"});
    CHECK(fields(run("@name ~ Engines", src)) == std::vector<std::string>{
        "General/Title: Talk about Engines", "Video/Title: Talk about Engines"});
    CHECK_FALSE(matches("@who ~ Nobody", src));
}

TEST_CASE("aliases support exists tests and are case-insensitive") {
    FakeSource none;
    none.add(TrackKind::General, {text("Format", "Matroska")});
    CHECK(matches("not @when", none));
    CHECK_FALSE(matches("@when", none));
    auto src = typicalVideo();
    CHECK(matches("@WHEN = 2024", src));
    CHECK(matches("@When", src));
}

TEST_CASE("an unknown or track-scoped alias is rejected when the evaluator is built") {
    ExprPtr unknown = parse("@bogus = 1");
    CHECK_THROWS_AS(Evaluator(*unknown, EvalOptions{}), EvalError);
    ExprPtr scoped = parse("General/@when = 1");
    CHECK_THROWS_AS(Evaluator(*scoped, EvalOptions{}), EvalError);
}

TEST_CASE("@when includes dates recognized in the file name, and bare patterns skip the Path track") {
    FakeSource src;
    src.add(TrackKind::General, {text("Title", "Recap")});
    src.add(TrackKind::Path, {text("When", "2019"), integer("Season", "2"), text("Id", "WcHWQnoE95w")});
    CHECK(fields(run("@when = 2019", src)) == std::vector<std::string>{"Path/When: 2019"});
    CHECK(matches("Path/Season = 2", src));
    CHECK(matches("Season >= 2", src));
    CHECK_FALSE(matches("'WcHWQnoE95w'", src));
    CHECK(matches("Path/Id = WcHWQnoE95w", src));
}

TEST_CASE("regex escapes in quoted strings reach the regex engine") {
    FakeSource digits;
    digits.add(TrackKind::General, {text("Title", "123")});
    FakeSource letters;
    letters.add(TrackKind::General, {text("Title", "ddd")});
    // '\d' used to lose its backslash and compile as 'd'.
    CHECK(matches("Title ~ \"^\\d+$\"", digits));
    CHECK_FALSE(matches("Title ~ \"^\\d+$\"", letters));
    CHECK(matches("Title ~ '^\\d+$'", digits));
    CHECK(matches("Title ~ \"^\\\\d+$\"", digits));  // doubled backslashes still work

    FakeSource dotted;
    dotted.add(TrackKind::General, {text("Title", "a.b")});
    FakeSource other;
    other.add(TrackKind::General, {text("Title", "axb")});
    CHECK(matches("Title ~ \"^a\\.b$\"", dotted));
    CHECK_FALSE(matches("Title ~ \"^a\\.b$\"", other));
    // A quoted string that is a bare pattern gets the same treatment.
    CHECK(matches("\"^\\d+$\"", digits));
    CHECK_FALSE(matches("\"^\\d+$\"", letters));

    FakeSource quote;
    quote.add(TrackKind::General, {text("Title", "a\"b"), text("Other", "\\")});
    CHECK(matches("Title = \"a\\\"b\"", quote));
    CHECK(matches("Other = \"\\\\\"", quote));
}
