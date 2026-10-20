#include <thread>

#include "cli.h"
#include "doctest.h"

using namespace mediagrep;

namespace {

Options parse(std::vector<std::string> args) {
    std::string error;
    auto opts = parseArgs(args, error);
    if (!opts) FAIL("parseArgs failed: " << error);
    return *opts;
}

std::string parseError(std::vector<std::string> args) {
    std::string error;
    auto opts = parseArgs(args, error);
    if (opts) FAIL("parseArgs unexpectedly succeeded");
    return error;
}

Match matchWith(std::vector<MatchedField> fields) {
    Match m;
    m.matched = true;
    m.fields = std::move(fields);
    return m;
}

}  // namespace

TEST_CASE("positional pattern then paths") {
    Options o = parse({"interesting", "a", "b"});
    REQUIRE(o.pattern);
    CHECK(*o.pattern == "interesting");
    CHECK(o.paths == std::vector<std::string>{"a", "b"});
    CHECK(o.expressions.empty());
}

TEST_CASE("-e expressions are collected and all positionals are paths") {
    Options o = parse({"-e", "a = 1", "-e", "b = 2", "x", "y"});
    CHECK(o.expressions == std::vector<std::string>{"a = 1", "b = 2"});
    CHECK_FALSE(o.pattern);
    CHECK(o.paths == std::vector<std::string>{"x", "y"});
}

TEST_CASE("-e accepts an attached argument") {
    Options o = parse({"-ea = 1", "."});
    CHECK(o.expressions == std::vector<std::string>{"a = 1"});
}

TEST_CASE("no paths is an error") {
    CHECK(parseError({"-e", "a = 1"}).find("PATH") != std::string::npos);
    CHECK(parseError({"pattern"}).find("PATH") != std::string::npos);
}

TEST_CASE("recursion is opt-in: -r, or -R to follow symlinks") {
    CHECK_FALSE(parse({"pat", "p", "d"}).recurse);
    CHECK(parse({"-r", "pat", "p", "d"}).recurse);
    CHECK_FALSE(parse({"-r", "pat", "p", "d"}).followSymlinks);
    CHECK(parse({"-R", "pat", "p", "d"}).recurse);
    CHECK(parse({"-R", "pat", "p", "d"}).followSymlinks);
}

TEST_CASE("short flags combine") {
    Options o = parse({"-ilR0", "pat", "d"});
    CHECK(o.ignoreCase);
    CHECK(o.output == Options::Output::FilesWithMatches);
    CHECK(o.followSymlinks);
    CHECK(o.nullSeparated);
}

TEST_CASE("output mode flags") {
    CHECK(parse({"-L", "p", "d"}).output == Options::Output::FilesWithoutMatch);
    CHECK(parse({"-c", "p", "d"}).output == Options::Output::Count);
    CHECK(parse({"-v", "p", "d"}).invert);
    CHECK(parse({"-q", "p", "d"}).quiet);
    CHECK(parse({"-s", "p", "d"}).noMessages);
    CHECK(parse({"-r", "p", "d"}).followSymlinks == false);
}

TEST_CASE("--progress and --no-progress override the automatic choice") {
    CHECK(parse({"pat", "d"}).progress == Options::Progress::Auto);
    CHECK(parse({"--progress", "pat", "d"}).progress == Options::Progress::On);
    CHECK(parse({"--no-progress", "pat", "d"}).progress == Options::Progress::Off);
    CHECK(parse({"--progress", "--no-progress", "pat", "d"}).progress == Options::Progress::Off);
}

TEST_CASE("-j takes a number, attached or separate") {
    CHECK(parse({"-j", "4", "p", "d"}).jobs == 4);
    CHECK(parse({"-j8", "p", "d"}).jobs == 8);
    CHECK(parseError({"-j", "x", "p", "d"}).find("-j") != std::string::npos);
    CHECK(parseError({"-j", "0", "p", "d"}).find("-j") != std::string::npos);
}

TEST_CASE("long options with = or a separate value") {
    Options o = parse({"--include=*.mkv", "--include", "*.mp4", "--exclude=*.txt",
                       "--parse-speed=0.5", "p", "d"});
    CHECK(o.include == std::vector<std::string>{"*.mkv", "*.mp4"});
    CHECK(o.exclude == std::vector<std::string>{"*.txt"});
    CHECK(o.parseSpeed == 0.5);
    CHECK(parse({"--parse-speed", "1", "p", "d"}).parseSpeed == 1.0);
    CHECK(parseError({"--parse-speed", "2", "p", "d"}).find("parse-speed") != std::string::npos);
    CHECK(parseError({"--parse-speed", "abc", "p", "d"}).find("parse-speed") != std::string::npos);
    CHECK(parseError({"--parse-speed", "nan", "p", "d"}).find("parse-speed") != std::string::npos);
}

TEST_CASE("--fields takes a file and needs no expression") {
    Options o = parse({"--fields", "x.mkv"});
    REQUIRE(o.fieldsFile);
    CHECK(*o.fieldsFile == "x.mkv");
    CHECK(parse({"--fields=x.mkv"}).fieldsFile.value() == "x.mkv");
}

TEST_CASE("help and version") {
    CHECK(parse({"--help"}).help);
    CHECK(parse({"-h"}).help);
    CHECK(parse({"--version"}).version);
}

TEST_CASE("-- ends option parsing") {
    Options o = parse({"-e", "a = 1", "--", "-weird-dir"});
    CHECK(o.paths == std::vector<std::string>{"-weird-dir"});
    Options p = parse({"--", "-pattern", "dir"});
    CHECK(p.pattern.value() == "-pattern");
}

TEST_CASE("errors: unknown options, missing values, missing expression") {
    CHECK(parseError({"-Z", "p", "d"}).find("-Z") != std::string::npos);
    CHECK(parseError({"--bogus", "p", "d"}).find("--bogus") != std::string::npos);
    CHECK(parseError({"-e"}).find("-e") != std::string::npos);
    CHECK(parseError({"--include"}).find("--include") != std::string::npos);
    CHECK(parseError({}).find("expression") != std::string::npos);
    CHECK(parseError({"-i"}).find("expression") != std::string::npos);
}

TEST_CASE("default jobs is at most 2") {
    CHECK(defaultJobs() >= 1);
    CHECK(defaultJobs() <= 2);
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw >= 2) CHECK(defaultJobs() == 2);
}

TEST_CASE("usage text names the program") {
    CHECK(usageText().find("mediagrep") != std::string::npos);
    CHECK(usageText().find("-e") != std::string::npos);
}

TEST_CASE("format: default output is one line per matched field") {
    Options o;
    Match m = matchWith({{TrackKind::General, 0, false, "Title", "Foo"},
                         {TrackKind::Audio, 1, true, "Language", "ja"}});
    CHECK(formatFile(o, "x.mkv", m) == "x.mkv: General/Title: Foo\nx.mkv: Audio[2]/Language: ja\n");
}

TEST_CASE("format: a match with no fields prints the path alone") {
    Options o;
    Match m = matchWith({});
    CHECK(formatFile(o, "x.mkv", m) == "x.mkv\n");
}

TEST_CASE("format: non-matching files print nothing by default") {
    Options o;
    CHECK(formatFile(o, "x.mkv", Match{}).empty());
}

TEST_CASE("format: -l prints the path once") {
    Options o;
    o.output = Options::Output::FilesWithMatches;
    Match m = matchWith({{TrackKind::General, 0, false, "Title", "Foo"},
                         {TrackKind::Video, 0, false, "Height", "720"}});
    CHECK(formatFile(o, "x.mkv", m) == "x.mkv\n");
    CHECK(formatFile(o, "x.mkv", Match{}).empty());
}

TEST_CASE("format: -L prints non-matching paths") {
    Options o;
    o.output = Options::Output::FilesWithoutMatch;
    CHECK(formatFile(o, "x.mkv", Match{}) == "x.mkv\n");
    CHECK(formatFile(o, "x.mkv", matchWith({})).empty());
}

TEST_CASE("format: -v inverts and drops fields") {
    Options o;
    o.invert = true;
    CHECK(formatFile(o, "x.mkv", Match{}) == "x.mkv\n");
    CHECK(formatFile(o, "x.mkv", matchWith({{TrackKind::General, 0, false, "Title", "Foo"}})).empty());
}

TEST_CASE("format: -0 terminates records with NUL") {
    Options o;
    o.nullSeparated = true;
    Match m = matchWith({{TrackKind::General, 0, false, "Title", "Foo"}});
    CHECK(formatFile(o, "x.mkv", m) == std::string("x.mkv: General/Title: Foo\0", 26));
    o.output = Options::Output::FilesWithMatches;
    CHECK(formatFile(o, "x.mkv", m) == std::string("x.mkv\0", 6));
}

TEST_CASE("format: -q prints nothing") {
    Options o;
    o.quiet = true;
    CHECK(formatFile(o, "x.mkv", matchWith({{TrackKind::General, 0, false, "Title", "Foo"}})).empty());
}

TEST_CASE("isSelected applies -v first, then the -L complement") {
    using Out = Options::Output;
    Match yes;
    yes.matched = true;
    const Match no;
    struct Row {
        Out output;
        bool invert;
        bool matchedFile;
        bool selected;
    };
    const Row rows[] = {
        {Out::Fields, false, true, true},
        {Out::Fields, false, false, false},
        {Out::Fields, true, true, false},
        {Out::Fields, true, false, true},
        {Out::FilesWithMatches, false, true, true},
        {Out::FilesWithMatches, true, true, false},
        {Out::FilesWithMatches, true, false, true},
        {Out::Count, false, true, true},
        {Out::Count, true, true, false},
        {Out::FilesWithoutMatch, false, true, false},
        {Out::FilesWithoutMatch, false, false, true},
        {Out::FilesWithoutMatch, true, true, true},   // -vL: files that match
        {Out::FilesWithoutMatch, true, false, false},
    };
    for (const Row& r : rows) {
        Options o;
        o.output = r.output;
        o.invert = r.invert;
        CHECK(isSelected(o, r.matchedFile ? yes : no) == r.selected);
        // The printed output agrees with the selection for -l and -L.
        if (r.output == Out::FilesWithMatches || r.output == Out::FilesWithoutMatch)
            CHECK(formatFile(o, "x.mkv", r.matchedFile ? yes : no).empty() == !r.selected);
    }
}
