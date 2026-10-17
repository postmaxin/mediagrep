// End-to-end tests: run the built mediagrep binary against the fixtures.
#include <algorithm>
#include <cstdio>
#include <string>

#include "doctest.h"

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <sys/wait.h>
#define POPEN popen
#define PCLOSE pclose
#endif

namespace {

struct Run {
    int status;
    std::string out;  // stdout and stderr combined, backslashes normalized to '/'
};

// Runs `mediagrep <args>` with the fixtures directory as the working directory.
Run run(const std::string& args) {
    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d \"" MEDIAGREP_FIXTURES_DIR "\" && \"" MEDIAGREP_BINARY "\" " + args + " 2>&1";
    // cmd.exe needs the whole thing wrapped in quotes when it contains quotes.
    cmd = "\"" + cmd + "\"";
#else
    cmd = "cd \"" MEDIAGREP_FIXTURES_DIR "\" && \"" MEDIAGREP_BINARY "\" " + args + " 2>&1";
#endif
    FILE* f = POPEN(cmd.c_str(), "r");
    REQUIRE(f != nullptr);
    std::string out;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    int status = PCLOSE(f);
#ifndef _WIN32
    if (WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    std::replace(out.begin(), out.end(), '\\', '/');
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    return {status, out};
}

}  // namespace

TEST_CASE("cli: field comparison prints path, field and value") {
    Run r = run("-e \"Audio/Language = ja\" talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out == "talk.mkv: Audio[2]/Language: ja\n");
}

TEST_CASE("cli: directories recurse, non-media files are skipped, order is by name") {
    Run r = run("-e \"Format\" --include \"*.mp4\" --include \"*.mkv\" .");
    CHECK(r.status == 0);
    CHECK(r.out == "./clip.mp4: General/Format: MPEG-4\n"
                   "./clip.mp4: Video/Format: AVC\n"
                   "./clip.mp4: Audio/Format: AAC\n"
                   "./talk.mkv: General/Format: Matroska\n"
                   "./talk.mkv: Video/Format: AVC\n"
                   "./talk.mkv: Audio[1]/Format: AAC\n"
                   "./talk.mkv: Audio[2]/Format: AAC\n");
}

TEST_CASE("cli: -l lists matching paths once") {
    Run r = run("-l -e \"Video/Height < 1000\" .");
    CHECK(r.status == 0);
    CHECK(r.out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: positional pattern searches every field, -i ignores case") {
    CHECK(run("\"interesting engineering\" .").status == 1);
    Run r = run("-i \"interesting engineering\" .");
    CHECK(r.status == 0);
    CHECK(r.out == "./talk.mkv: General/PUBLISHER: Interesting Engineering\n");
}

TEST_CASE("cli: no path means the current directory") {
    Run r = run("-l -e \"Format = Matroska\"");
    CHECK(r.status == 0);
    CHECK(r.out == "./talk.mkv\n");
}

TEST_CASE("cli: multiple -e are ORed") {
    Run r = run("-l -e \"Audio/Language = ja\" -e \"Height = 720\" .");
    CHECK(r.out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: -v and -L select non-matching media files") {
    CHECK(run("-v -l -e \"Audio/Language = ja\" .").out == "./clip.mp4\n./song.mp3\n");
    CHECK(run("-L -e \"Audio/Language = ja\" .").out == "./clip.mp4\n./song.mp3\n");
}

TEST_CASE("cli: -c counts matching files per path argument") {
    Run r = run("-c -e \"Video/Height < 1000\" . talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out == ".: 2\ntalk.mkv: 1\n");
}

TEST_CASE("cli: -0 separates records with NUL") {
    Run r = run("-l0 -e \"Audio/Language = ja\" .");
    CHECK(r.out == std::string("./talk.mkv\0", 11));
}

TEST_CASE("cli: -q prints nothing and exits by match status") {
    Run hit = run("-q -e \"Audio/Language = ja\" .");
    CHECK(hit.status == 0);
    CHECK(hit.out.empty());
    CHECK(run("-q -e \"Audio/Language = fr\" .").status == 1);
}

TEST_CASE("cli: no match exits 1 with no output") {
    Run r = run("-e \"Audio/Language = fr\" .");
    CHECK(r.status == 1);
    CHECK(r.out.empty());
}

TEST_CASE("cli: a bad expression exits 2 with the position marked") {
    Run r = run("-e \"Title =\" .");
    CHECK(r.status == 2);
    CHECK(r.out.find("expected value") != std::string::npos);
    CHECK(r.out.find("^") != std::string::npos);
}

TEST_CASE("cli: a missing path warns, continues, and exits 2") {
    Run r = run("-l -e \"Audio/Language = ja\" nope.mkv talk.mkv");
    CHECK(r.status == 2);
    CHECK(r.out.find("nope.mkv") != std::string::npos);
    CHECK(r.out.find("talk.mkv\n") != std::string::npos);
    Run quiet = run("-s -l -e \"Audio/Language = ja\" nope.mkv talk.mkv");
    CHECK(quiet.status == 2);
    CHECK(quiet.out == "talk.mkv\n");
}

TEST_CASE("cli: --fields dumps every field of one file") {
    Run r = run("--fields talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out.find("General/Format: Matroska\n") != std::string::npos);
    CHECK(r.out.find("Audio[2]/Language: ja\n") != std::string::npos);
    CHECK(r.out.find("Video/Height: 32\n") != std::string::npos);
}

TEST_CASE("cli: --version names both the tool and the library") {
    Run r = run("--version");
    CHECK(r.status == 0);
    CHECK(r.out.find("mediagrep") != std::string::npos);
    CHECK(r.out.find("MediaInfoLib") != std::string::npos);
}

TEST_CASE("cli: no expression is a usage error") {
    Run r = run("");
    CHECK(r.status == 2);
    CHECK(r.out.find("usage") != std::string::npos);
}

TEST_CASE("cli: a bad regex exits 2") {
    CHECK(run("-e \"Title ~ '('\" .").status == 2);
}

TEST_CASE("cli: non-ASCII file names are read and printed as UTF-8") {
    Run r = run("-l -e \"Format = Matroska\" ../unicode");
    CHECK(r.status == 0);
    CHECK(r.out == "../unicode/" "t\xC3\xA1lk\xE2\x80\x93" "engines.mkv" "\n");
}

TEST_CASE("cli: -j 1 gives the same output") {
    CHECK(run("-j 1 -l -e \"Video/Height < 1000\" .").out == "./clip.mp4\n./talk.mkv\n");
}
