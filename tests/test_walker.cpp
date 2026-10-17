#include <fstream>

#include "doctest.h"
#include "walker.h"

using namespace mediagrep;
namespace fs = std::filesystem;

namespace {

// A throwaway tree:
//   root/a/1.mkv  root/a/b/2.mp4  root/a/b/3.txt  root/a/c/ (empty)  root/a/z.mkv
//   root/top.mp3
struct Tree {
    fs::path root;
    Tree() {
        root = fs::temp_directory_path() / fs::path("mediagrep-walk-" + std::to_string(counter()++));
        fs::remove_all(root);
        fs::create_directories(root / "a" / "b");
        fs::create_directories(root / "a" / "c");
        touch(root / "a" / "1.mkv");
        touch(root / "a" / "b" / "2.mp4");
        touch(root / "a" / "b" / "3.txt");
        touch(root / "a" / "z.mkv");
        touch(root / "top.mp3");
    }
    ~Tree() { fs::remove_all(root); }
    static void touch(const fs::path& p) { std::ofstream(p) << "x"; }
    static int& counter() { static int c = 0; return c; }
};

std::vector<std::string> files(const std::vector<fs::path>& roots, const WalkOptions& opts = {},
                               std::vector<std::string>* errors = nullptr) {
    std::vector<std::string> out;
    walk(roots, opts,
         [&](const fs::path& p) { out.push_back(p.generic_string()); },
         [&](const fs::path& p, const std::string& msg) {
             if (errors) errors->push_back(p.generic_string() + ": " + msg);
         });
    return out;
}

std::string rel(const Tree& t, const std::string& p) {
    const std::string prefix = t.root.generic_string() + "/";
    return p.rfind(prefix, 0) == 0 ? p.substr(prefix.size()) : p;
}

std::vector<std::string> relFiles(const Tree& t, const std::vector<fs::path>& roots,
                                  const WalkOptions& opts = {}) {
    std::vector<std::string> out;
    for (const auto& p : files(roots, opts)) out.push_back(rel(t, p));
    return out;
}

}  // namespace

TEST_CASE("glob: literal, star, question mark") {
    CHECK(globMatch("a.mkv", "a.mkv"));
    CHECK_FALSE(globMatch("a.mkv", "b.mkv"));
    CHECK(globMatch("*.mkv", "a.mkv"));
    CHECK(globMatch("*.mkv", ".mkv"));
    CHECK_FALSE(globMatch("*.mkv", "a.mp4"));
    CHECK(globMatch("?.mkv", "a.mkv"));
    CHECK_FALSE(globMatch("?.mkv", "ab.mkv"));
    CHECK(globMatch("*", "anything"));
    CHECK(globMatch("a*b*c", "aXXbYYc"));
    CHECK_FALSE(globMatch("a*b*c", "aXXbYY"));
    CHECK(globMatch("", ""));
    CHECK_FALSE(globMatch("", "a"));
}

TEST_CASE("glob: bracket classes") {
    CHECK(globMatch("[ab]*", "apple"));
    CHECK(globMatch("[ab]*", "banana"));
    CHECK_FALSE(globMatch("[ab]*", "cherry"));
    CHECK(globMatch("*.[mM][kK][vV]", "x.MKV"));
    CHECK(globMatch("[a-c]*", "b"));
    CHECK_FALSE(globMatch("[a-c]*", "d"));
    CHECK(globMatch("[!a]*", "b"));
    CHECK_FALSE(globMatch("[!a]*", "a"));
}

TEST_CASE("glob is case-sensitive") {
    CHECK_FALSE(globMatch("*.mkv", "A.MKV"));
}

TEST_CASE("walk visits files depth-first in name order") {
    Tree t;
    CHECK(relFiles(t, {t.root}) == std::vector<std::string>{
        "a/1.mkv", "a/b/2.mp4", "a/b/3.txt", "a/z.mkv", "top.mp3"});
}

TEST_CASE("walk reports paths as the root given plus the relative part") {
    Tree t;
    auto out = files({t.root / "a"});
    REQUIRE(out.size() == 4);
    CHECK(out[0] == (t.root / "a" / "1.mkv").generic_string());
}

TEST_CASE("walk honours include and exclude globs on the file name") {
    Tree t;
    WalkOptions inc;
    inc.include = {"*.mkv"};
    CHECK(relFiles(t, {t.root}, inc) == std::vector<std::string>{"a/1.mkv", "a/z.mkv"});

    WalkOptions exc;
    exc.exclude = {"*.txt", "*.mp3"};
    CHECK(relFiles(t, {t.root}, exc) == std::vector<std::string>{"a/1.mkv", "a/b/2.mp4", "a/z.mkv"});

    WalkOptions both;
    both.include = {"*.mkv", "*.mp4"};
    both.exclude = {"1.*"};
    CHECK(relFiles(t, {t.root}, both) == std::vector<std::string>{"a/b/2.mp4", "a/z.mkv"});
}

TEST_CASE("roots are visited in argument order, and file roots bypass filters") {
    Tree t;
    WalkOptions inc;
    inc.include = {"*.mkv"};
    CHECK(relFiles(t, {t.root / "top.mp3", t.root / "a" / "b", t.root / "a" / "1.mkv"}, inc) ==
          std::vector<std::string>{"top.mp3", "a/1.mkv"});
}

TEST_CASE("a missing root is an error and the walk continues") {
    Tree t;
    std::vector<std::string> errors;
    auto out = files({t.root / "nope", t.root / "top.mp3"}, {}, &errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("nope") != std::string::npos);
    REQUIRE(out.size() == 1);
    CHECK(rel(t, out[0]) == "top.mp3");
}

TEST_CASE("walk returns the error count") {
    Tree t;
    WalkOptions opts;
    CHECK(walk({t.root / "nope", t.root / "also-nope"}, opts, [](const fs::path&) {},
               [](const fs::path&, const std::string&) {}) == 2);
}
