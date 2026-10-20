#include <string>

#include "doctest.h"
#include "progress.h"

using namespace mediagrep;

namespace {

ProgressState state(std::size_t done, std::size_t found, bool walking, std::size_t matched,
                    double elapsed, std::string current = "") {
    ProgressState s;
    s.done = done;
    s.found = found;
    s.walking = walking;
    s.matched = matched;
    s.elapsed = elapsed;
    s.current = std::move(current);
    return s;
}

const char* kLongName =
    "Business Insider/Why the Mold Behind Brie Cheese Is Disappearing [-KObTYIAlGI].mkv";

}  // namespace

TEST_CASE("durations are m:ss under an hour and h:mm:ss above") {
    CHECK(formatDuration(0) == "0:00");
    CHECK(formatDuration(42) == "0:42");
    CHECK(formatDuration(61.9) == "1:01");
    CHECK(formatDuration(3723) == "1:02:03");
    CHECK(formatDuration(-5) == "0:00");
}

TEST_CASE("display width counts code points, with East Asian wide characters as two") {
    CHECK(displayWidth("abc") == 3);
    CHECK(displayWidth("t\xC3\xA1lk") == 4);           // á
    CHECK(displayWidth("Food\xEF\xBC\x9F") == 6);       // fullwidth question mark
    CHECK(displayWidth("\xE4\xB8\xAD\xE6\x96\x87") == 4);  // two CJK ideographs
    CHECK(displayWidth("") == 0);
}

TEST_CASE("while walking, the total is a lower bound and the time is elapsed") {
    CHECK(formatProgressLine(state(120, 340, true, 3, 42), 80) == "120/340+ files, 3 matched, 0:42");
}

TEST_CASE("once the walk is done: percentage, bar, and time left") {
    // 60 s for 85 files leaves 255 files, about 180 s.
    CHECK(formatProgressLine(state(85, 340, false, 3, 60), 80) ==
          "25% [#####---------------] 85/340 files, 3 matched, 3:00 left");
}

TEST_CASE("errors are shown when there are any") {
    ProgressState s = state(10, 20, true, 1, 5);
    s.errors = 2;
    CHECK(formatProgressLine(s, 80) == "10/20+ files, 1 matched, 2 errors, 0:05");
    s.errors = 1;
    CHECK(formatProgressLine(s, 80) == "10/20+ files, 1 matched, 1 error, 0:05");
}

TEST_CASE("the current file fills the remaining width, truncated from the left") {
    const std::string line = formatProgressLine(state(85, 340, false, 3, 60, kLongName), 80);
    CHECK(line == "25% [#####---------------] 85/340 files, 3 matched, 3:00 left"
                  "  ...bTYIAlGI].mkv");
    CHECK(displayWidth(line) == 79);
    CHECK(formatProgressLine(state(1, 2, true, 0, 1, "a/b.mkv"), 80) ==
          "1/2+ files, 0 matched, 0:01  a/b.mkv");
}

TEST_CASE("narrow terminals drop the bar, then the details, then truncate") {
    CHECK(formatProgressLine(state(85, 340, false, 3, 60), 50) ==
          "25% 85/340 files, 3 matched, 3:00 left");
    CHECK(formatProgressLine(state(85, 340, false, 3, 60), 30) == "85/340 files, 3 matched");
    CHECK(formatProgressLine(state(85, 340, false, 3, 60), 14) == "85/340 files");
    CHECK(formatProgressLine(state(85, 340, false, 3, 60), 8) == "85/340 ");
}

TEST_CASE("the line never reaches the last column, at any width") {
    const ProgressState states[] = {
        state(0, 0, true, 0, 0),
        state(120, 340, true, 3, 42, kLongName),
        state(85, 340, false, 3, 60, kLongName),
        state(339, 340, false, 300, 7200, "Food\xEF\xBC\x9F\xEF\xBC\x9F\xEF\xBC\x9F.mkv"),
    };
    for (const ProgressState& s : states)
        for (std::size_t width = 0; width <= 140; ++width) {
            const std::size_t limit = width == 0 ? 0 : width - 1;
            CHECK(displayWidth(formatProgressLine(s, width)) <= limit);
        }
}

TEST_CASE("control characters in file names cannot break the line") {
    const std::string line = formatProgressLine(state(1, 2, true, 0, 1, "a\nb\rc\x1b.mkv"), 80);
    CHECK(line.find_first_of("\n\r\x1b") == std::string::npos);
    CHECK(line.find("a?b?c?.mkv") != std::string::npos);
}

TEST_CASE("log lines always report elapsed time") {
    CHECK(formatProgressLog(state(120, 340, true, 3, 42)) ==
          "progress: 120/340+ files, 3 matched, 0:42 elapsed");
    CHECK(formatProgressLog(state(340, 340, false, 5, 133)) ==
          "progress: 340/340 files, 5 matched, 2:13 elapsed");
}

TEST_CASE("emoji and symbols that East Asian Width lists as wide take two columns") {
    CHECK(displayWidth("\xE2\x9C\x85") == 2);      // U+2705 white heavy check mark
    CHECK(displayWidth("\xE2\xAD\x90") == 2);      // U+2B50 star
    CHECK(displayWidth("\xE2\x9A\xA1") == 2);      // U+26A1 high voltage
    CHECK(displayWidth("\xE2\x8C\x9A") == 2);      // U+231A watch
    CHECK(displayWidth("\xE3\x80\x88") == 2);      // U+3008 left angle bracket
    CHECK(displayWidth("\xF0\x9F\x8E\xB5") == 2);  // U+1F3B5 musical note
    // Neighbours that are not wide: U+2713 check mark, U+303F, U+FF61 halfwidth full stop.
    CHECK(displayWidth("\xE2\x9C\x93") == 1);
    CHECK(displayWidth("\xE3\x80\xBF") == 1);
    CHECK(displayWidth("\xEF\xBD\xA1") == 1);
}

TEST_CASE("redraw pads over a longer old line, but never past the last column") {
    // Unchanged terminal width: pad the 10 columns drawn before to 10.
    CHECK(redrawText("abcd", 10, 80) == "\rabcd      ");
    // Shrunk from 80 to 40: 79 columns were drawn, but padding stops at 39.
    const std::string r = redrawText("abcd", 79, 40);
    CHECK(r.size() == 1 + 39);
    CHECK(displayWidth(r.substr(1)) == 39);
    // A line already at the limit gets no padding, and a zero width gets none.
    CHECK(redrawText("abcd", 79, 5) == "\rabcd");
    CHECK(redrawText("abcd", 79, 0) == "\rabcd");
}

TEST_CASE("clear never writes more than the current width minus one") {
    CHECK(clearText(10, 80) == "\r" + std::string(10, ' ') + "\r");
    CHECK(clearText(79, 40) == "\r" + std::string(39, ' ') + "\r");
    CHECK(clearText(79, 1) == "\r\r");
    CHECK(clearText(79, 0) == "\r\r");
}
