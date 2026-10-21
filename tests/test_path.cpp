#include <map>

#include "doctest.h"
#include "path_track.h"

using namespace mediagrep;

namespace {

std::map<std::string, std::string> fields(const std::string& name, const std::string& dir = "Videos") {
    std::map<std::string, std::string> out;
    for (const auto& f : recognizePath(name, dir)) out[f.name] = f.value;
    return out;
}

std::string when(const std::string& name) {
    auto f = fields(name);
    return f.count("When") ? f["When"] : "";
}

}  // namespace

TEST_CASE("full dates in a name, with -, _ or . separators") {
    CHECK(when("2024-03-01 Talk.mkv") == "2024-03-01");
    CHECK(when("Talk 2024_03_01.mkv") == "2024-03-01");
    CHECK(when("Talk.2024.03.01.1080p.mkv") == "2024-03-01");
    CHECK(when("Talk [2024-03-01].mkv") == "2024-03-01");
}

TEST_CASE("compact dates need a plausible month and day and must be delimited") {
    CHECK(when("Talk 20240301.mkv") == "2024-03-01");
    CHECK(when("IMG_20240301_120000.mp4") == "2024-03-01");
    CHECK(when("Talk 20241301.mkv") == "");         // month 13
    CHECK(when("Talk 120240301.mkv") == "");        // nine digits
    CHECK(when("Talk x20240301y.mkv") == "");       // not delimited
}

TEST_CASE("a parenthesized or delimited year") {
    CHECK(when("The Movie (2019).mkv") == "2019");
    CHECK(when("The Movie 2019 1080p.mkv") == "2019");
    CHECK(when("2001 A Space Odyssey.mkv") == "2001");
    CHECK(when("The.Movie.2019.mkv") == "2019");
}

TEST_CASE("digits that are not a year") {
    CHECK(when("Talk 1080p.mkv") == "");
    CHECK(when("Talk 2160p.mkv") == "");
    CHECK(when("Talk [2024abcdefg].mkv") == "");    // inside an id
    CHECK(when("Talk 12019.mkv") == "");            // five digits
    CHECK(when("Episode 1234.mkv") == "");          // out of range
    CHECK(when("plain name.mkv") == "");
}

TEST_CASE("a full date wins over a bare year elsewhere in the name") {
    CHECK(when("2019 recap 2024-03-01.mkv") == "2024-03-01");
}

TEST_CASE("season and episode from S01E02 and 1x02") {
    auto f = fields("Show - S01E02 - Title.mkv");
    CHECK(f["Season"] == "1");
    CHECK(f["Episode"] == "2");
    auto g = fields("show.s02e10.title.mkv");
    CHECK(g["Season"] == "2");
    CHECK(g["Episode"] == "10");
    auto h = fields("Show 3x07 Title.mkv");
    CHECK(h["Season"] == "3");
    CHECK(h["Episode"] == "7");
    CHECK(fields("Show 1920x1080.mkv").count("Season") == 0);
    CHECK(fields("Talk.mkv").count("Episode") == 0);
}

TEST_CASE("season from the parent directory when the name has none") {
    CHECK(fields("Episode 3.mkv", "Season 2")["Season"] == "2");
    CHECK(fields("Episode 3.mkv", "season_02")["Season"] == "2");
    CHECK(fields("Episode 3.mkv", "S03")["Season"] == "3");
    CHECK(fields("Show - S01E02.mkv", "Season 2")["Season"] == "1");  // name wins
    CHECK(fields("Episode 3.mkv", "Videos").count("Season") == 0);
}

TEST_CASE("the yt-dlp id suffix") {
    CHECK(fields("Why You Don't Trust Tap Water [WcHWQnoE95w].mkv")["Id"] == "WcHWQnoE95w");
    CHECK(fields("Talk [abc-DEF_123].webm")["Id"] == "abc-DEF_123");
    CHECK(fields("Talk [2024-03-01].mkv").count("Id") == 0);   // wrong shape
    CHECK(fields("Talk [tooshort].mkv").count("Id") == 0);
}

TEST_CASE("field types") {
    auto all = recognizePath("Show - S01E02 - Title 2024-03-01 [WcHWQnoE95w].mkv", "");
    REQUIRE(all.size() == 4);
    for (const auto& f : all) {
        if (f.name == "Season" || f.name == "Episode") CHECK(f.type == ValueType::Integer);
        else CHECK(f.type == ValueType::Text);
        CHECK(f.measure.empty());
    }
}

TEST_CASE("the id is the bracketed word just before the extensions, not the first one") {
    CHECK(fields("Practical Engineering [Engineering] [WcHWQnoE95w].mkv")["Id"] == "WcHWQnoE95w");
    CHECK(fields("[Engineering] Talk [WcHWQnoE95w].mkv")["Id"] == "WcHWQnoE95w");
    // A bracketed 11-character word that is not at the end is not an id.
    CHECK(fields("Talk [Engineering] more.mkv").count("Id") == 0);
    CHECK(fields("Talk [WcHWQnoE95w] more.mkv").count("Id") == 0);
}

TEST_CASE("ids followed by double extensions") {
    CHECK(fields("Talk [WcHWQnoE95w].f137.mp4")["Id"] == "WcHWQnoE95w");
    CHECK(fields("Talk [WcHWQnoE95w].en.vtt")["Id"] == "WcHWQnoE95w");
    CHECK(fields("Talk [WcHWQnoE95w].info.json")["Id"] == "WcHWQnoE95w");
    CHECK(fields("Talk [WcHWQnoE95w]")["Id"] == "WcHWQnoE95w");
    CHECK(fields("Talk [WcHWQnoE95w].mkv.part").count("Id") == 1);
}

TEST_CASE("a name with no id has none") {
    CHECK(fields("Talk about engines.mkv").count("Id") == 0);
    CHECK(fields("Talk [WcHWQnoE95w] extra.mkv").count("Id") == 0);
}

TEST_CASE("dates and episodes are not recognized inside the id") {
    auto year = fields("Talk [ab_2024_xyz].mkv");
    CHECK(year["Id"] == "ab_2024_xyz");
    CHECK(year.count("When") == 0);

    auto full = fields("Talk [2024-03-01x].mkv");  // 11 characters, a year inside
    CHECK(full["Id"] == "2024-03-01x");
    CHECK(full.count("When") == 0);

    auto episode = fields("Talk [-S01E02-abc].mkv", "Videos");
    CHECK(episode["Id"] == "-S01E02-abc");
    CHECK(episode.count("Season") == 0);
    CHECK(episode.count("Episode") == 0);

    auto nxn = fields("Talk [x1x02_abcde].mkv");
    CHECK(nxn.count("Episode") == 0);

    // The rest of the name is still read.
    auto both = fields("Show S01E02 2019-05-04 [ab_2024_xyz].mkv");
    CHECK(both["When"] == "2019-05-04");
    CHECK(both["Season"] == "1");
    CHECK(both["Episode"] == "2");
}
