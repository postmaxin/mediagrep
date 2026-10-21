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

// Runs a shell command line and captures its output and exit status.
Run runShell(const std::string& cmd) {
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
    return runShell(cmd);
}

}  // namespace

TEST_CASE("cli: field comparison prints path, field and value") {
    Run r = run("-e \"Audio/Language = ja\" talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out == "talk.mkv: Audio[2]/Language: ja\n");
}

TEST_CASE("cli: directories recurse, non-media files are skipped, order is by name") {
    Run r = run("-r -e \"Format\" --include \"*.mp4\" --include \"*.mkv\" .");
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
    Run r = run("-r -l -e \"Video/Height < 1000\" .");
    CHECK(r.status == 0);
    CHECK(r.out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: positional pattern searches every field, -i ignores case") {
    CHECK(run("-r \"interesting engineering\" .").status == 1);
    Run r = run("-r -i \"interesting engineering\" .");
    CHECK(r.status == 0);
    CHECK(r.out == "./talk.mkv: General/PUBLISHER: Interesting Engineering\n");
}

TEST_CASE("cli: no path is a usage error") {
    Run r = run("-l -e \"Format = Matroska\"");
    CHECK(r.status == 2);
    CHECK(r.out.find("usage") != std::string::npos);
}

TEST_CASE("cli: a directory without -r is an error, like grep") {
    Run r = run("-l -e \"Format = Matroska\" . talk.mkv");
    CHECK(r.status == 2);
    CHECK(r.out == "mediagrep: .: Is a directory\ntalk.mkv\n");
    CHECK(run("-R -l -e \"Format = Matroska\" .").out == "./talk.mkv\n");
}

TEST_CASE("cli: multiple -e are ORed") {
    Run r = run("-r -l -e \"Audio/Language = ja\" -e \"Height = 720\" .");
    CHECK(r.out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: -v and -L select non-matching media files") {
    CHECK(run("-r -v -l -e \"Audio/Language = ja\" .").out == "./clip.mp4\n./song.mp3\n");
    CHECK(run("-r -L -e \"Audio/Language = ja\" .").out == "./clip.mp4\n./song.mp3\n");
}

TEST_CASE("cli: -v -L prints what it selects, and the exit status agrees") {
    // talk.mkv matches; -v inverts, -L complements that, so it is selected.
    Run r = run("-v -L -e \"Audio/Language = ja\" talk.mkv");
    CHECK(r.out == "talk.mkv\n");
    CHECK(r.status == 0);
    // clip.mp4 does not match, so -v -L does not select it.
    Run n = run("-v -L -e \"Audio/Language = ja\" clip.mp4");
    CHECK(n.out.empty());
    CHECK(n.status == 1);
}

TEST_CASE("cli: -c counts matching files per path argument") {
    Run r = run("-r -c -e \"Video/Height < 1000\" . talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out == ".: 2\ntalk.mkv: 1\n");
}

TEST_CASE("cli: -0 separates records with NUL") {
    Run r = run("-r -l0 -e \"Audio/Language = ja\" .");
    CHECK(r.out == std::string("./talk.mkv\0", 11));
}

TEST_CASE("cli: -q prints nothing and exits by match status") {
    Run hit = run("-r -q -e \"Audio/Language = ja\" .");
    CHECK(hit.status == 0);
    CHECK(hit.out.empty());
    CHECK(run("-r -q -e \"Audio/Language = fr\" .").status == 1);
}

TEST_CASE("cli: no match exits 1 with no output") {
    Run r = run("-r -e \"Audio/Language = fr\" .");
    CHECK(r.status == 1);
    CHECK(r.out.empty());
}

TEST_CASE("cli: a bad expression exits 2 with the position marked") {
    Run r = run("-r -e \"Title =\" .");
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
    CHECK(run("-r -e \"Title ~ '('\" .").status == 2);
}

TEST_CASE("cli: non-ASCII file names are read and printed as UTF-8") {
    Run r = run("-r -l -e \"Format = Matroska\" ../unicode");
    CHECK(r.status == 0);
    CHECK(r.out == "../unicode/" "t\xC3\xA1lk\xE2\x80\x93" "engines.mkv" "\n");
}

TEST_CASE("cli: -j 1 gives the same output") {
    CHECK(run("-r -j 1 -l -e \"Video/Height < 1000\" .").out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: the File track is searchable and listed by --fields") {
    Run r = run("-e \"File/Size > 10K\" talk.mkv");
    CHECK(r.status == 0);
    CHECK(r.out == "talk.mkv: File/Size: 11932\n");
    CHECK(run("-r -l -e \"Size > 10K and Size < 20K\" .").out == "./talk.mkv\n");
    Run f = run("--fields talk.mkv");
    CHECK(f.out.find("File/Name: talk.mkv\n") != std::string::npos);
    CHECK(f.out.find("File/Size: 11932\n") != std::string::npos);
    CHECK(f.out.find("File/Modified: ") != std::string::npos);
}

TEST_CASE("cli: aliases expand and report the concrete field") {
    CHECK(run("-e \"@who ~ Engineering\" talk.mkv").out ==
          "talk.mkv: General/PUBLISHER: Interesting Engineering\n");
    CHECK(run("-e \"@name ~ talk\" talk.mkv").out == "talk.mkv: File/Name: talk.mkv\n");
    CHECK(run("-e \"@when\" -l song.mp3").out == "song.mp3\n");
    Run bad = run("-r -e \"@bogus = 1\" .");
    CHECK(bad.status == 2);
    CHECK(bad.out.find("unknown alias") != std::string::npos);
}

TEST_CASE("cli: the Path track is recognized from the file name") {
    Run r = run("-r -e \"Path/When = 2019\" ../unicode");
    CHECK(r.status == 0);
    CHECK(r.out == "../unicode/Quiet Song - S01E02 - 2019-05-04 [dQw4w9WgXcQ].mp3: Path/When: 2019-05-04\n");
    CHECK(run("-r -l -e \"@when = 2019\" ../unicode").out ==
          "../unicode/Quiet Song - S01E02 - 2019-05-04 [dQw4w9WgXcQ].mp3\n");
    Run f = run("--fields \"../unicode/Quiet Song - S01E02 - 2019-05-04 [dQw4w9WgXcQ].mp3\"");
    CHECK(f.out.find("Path/Season: 1\n") != std::string::npos);
    CHECK(f.out.find("Path/Episode: 2\n") != std::string::npos);
    CHECK(f.out.find("Path/Id: dQw4w9WgXcQ\n") != std::string::npos);
}

TEST_CASE("cli: --progress without a terminal writes a summary line to stderr") {
    Run r = run("-r --progress -l -e \"Video/Height < 1000\" .");
    CHECK(r.status == 0);
    CHECK(r.out.find("./clip.mp4\n./talk.mkv\n") != std::string::npos);
    CHECK(r.out.find("mediagrep: progress: 5/5 files, 2 matched, ") != std::string::npos);
    CHECK(run("-r -l -e \"Video/Height < 1000\" .").out == "./clip.mp4\n./talk.mkv\n");
}

TEST_CASE("cli: -q exits 0 on a match even when another path is an error, like grep") {
    CHECK(run("-q -e \"Audio/Language = ja\" talk.mkv nope.mkv").status == 0);
    CHECK(run("-q -e \"Audio/Language = fr\" talk.mkv nope.mkv").status == 2);
}

#ifndef _WIN32
TEST_CASE("cli: a write error on stdout exits 2 with a message, even when buffered") {
    // /dev/full accepts the write into stdio's buffer and fails it at the
    // final flush, so the failure only shows up if that flush is checked.
    if (std::FILE* full = std::fopen("/dev/full", "w"))
        std::fclose(full);
    else
        return;  // not Linux
    // Only stdout goes to /dev/full; stderr still reaches the pipe.
    Run r = runShell("cd \"" MEDIAGREP_FIXTURES_DIR "\" && { \"" MEDIAGREP_BINARY
                     "\" -r -l -e \"Video/Height < 1000\" . > /dev/full; } 2>&1");
    CHECK(r.status == 2);
    CHECK(r.out.find("mediagrep: write error") != std::string::npos);
}
#endif
