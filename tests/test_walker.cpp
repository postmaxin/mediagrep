#include <algorithm>

#include "doctest.h"
#include "fsys.h"
#include "tmp_tree.h"
#include "walker.h"

using namespace mediagrep;
using mediagrep::test::TempTree;

namespace {

// A throwaway tree:
//   root/a/1.mkv  root/a/b/2.mp4  root/a/b/3.txt  root/a/c/ (empty)  root/a/z.mkv
//   root/top.mp3
struct Tree {
    TempTree t{"mediagrep-walk"};
    Tree() {
        t.file("a/1.mkv");
        t.file("a/b/2.mp4");
        t.file("a/b/3.txt");
        t.dir("a/c");
        t.file("a/z.mkv");
        t.file("top.mp3");
    }
    std::string path(const std::string& rel) const { return t.path(rel); }
};

std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

WalkOptions recursive() {
    WalkOptions o;
    o.recurse = true;
    return o;
}

std::vector<std::string> files(const std::vector<std::string>& roots, const WalkOptions& opts = recursive(),
                               std::vector<std::string>* errors = nullptr) {
    std::vector<std::string> out;
    walk(roots, opts,
         [&](const std::string& p) { out.push_back(slashes(p)); },
         [&](const std::string& p, const std::string& msg) {
             if (errors) errors->push_back(slashes(p) + ": " + msg);
         });
    return out;
}

std::string rel(const Tree& t, const std::string& p) {
    const std::string prefix = slashes(t.t.root) + "/";
    return p.rfind(prefix, 0) == 0 ? p.substr(prefix.size()) : p;
}

std::vector<std::string> relFiles(const Tree& t, const std::vector<std::string>& roots,
                                  const WalkOptions& opts = recursive()) {
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
    CHECK(relFiles(t, {t.t.root}) == std::vector<std::string>{
        "a/1.mkv", "a/b/2.mp4", "a/b/3.txt", "a/z.mkv", "top.mp3"});
}

TEST_CASE("walk reports paths as the root given plus the relative part") {
    Tree t;
    auto out = files({t.path("a")});
    REQUIRE(out.size() == 4);
    CHECK(out[0] == slashes(t.path("a/1.mkv")));
}

TEST_CASE("walk honours include and exclude globs on the file name") {
    Tree t;
    WalkOptions inc = recursive();
    inc.include = {"*.mkv"};
    CHECK(relFiles(t, {t.t.root}, inc) == std::vector<std::string>{"a/1.mkv", "a/z.mkv"});

    WalkOptions exc = recursive();
    exc.exclude = {"*.txt", "*.mp3"};
    CHECK(relFiles(t, {t.t.root}, exc) == std::vector<std::string>{"a/1.mkv", "a/b/2.mp4", "a/z.mkv"});

    WalkOptions both = recursive();
    both.include = {"*.mkv", "*.mp4"};
    both.exclude = {"1.*"};
    CHECK(relFiles(t, {t.t.root}, both) == std::vector<std::string>{"a/b/2.mp4", "a/z.mkv"});
}

TEST_CASE("roots are visited in argument order, and file roots bypass filters") {
    Tree t;
    WalkOptions inc = recursive();
    inc.include = {"*.mkv"};
    CHECK(relFiles(t, {t.path("top.mp3"), t.path("a/b"), t.path("a/1.mkv")}, inc) ==
          std::vector<std::string>{"top.mp3", "a/1.mkv"});
}

TEST_CASE("a missing root is an error and the walk continues") {
    Tree t;
    std::vector<std::string> errors;
    auto out = files({t.path("nope"), t.path("top.mp3")}, recursive(), &errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("nope") != std::string::npos);
    REQUIRE(out.size() == 1);
    CHECK(rel(t, out[0]) == "top.mp3");
}

TEST_CASE("walk returns the error count") {
    Tree t;
    CHECK(walk({t.path("nope"), t.path("also-nope")}, recursive(), [](const std::string&) {},
               [](const std::string&, const std::string&) {}) == 2);
}

TEST_CASE("without recurse, a directory root is 'Is a directory' and file roots still visit") {
    Tree t;
    std::vector<std::string> errors;
    auto out = files({t.path("a"), t.path("top.mp3")}, WalkOptions{}, &errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == slashes(t.path("a")) + ": Is a directory");
    REQUIRE(out.size() == 1);
    CHECK(rel(t, out[0]) == "top.mp3");
}

TEST_CASE("walk stops early once cancelled") {
    Tree t;
    WalkOptions opts = recursive();
    std::vector<std::string> out;
    opts.cancelled = [&] { return !out.empty(); };
    walk({t.t.root, t.path("top.mp3")}, opts, [&](const std::string& p) { out.push_back(p); },
         [](const std::string&, const std::string&) {});
    CHECK(out.size() == 1);
}

#ifndef _WIN32
namespace {

WalkOptions following() {
    WalkOptions o = recursive();
    o.followSymlinks = true;
    return o;
}

struct Outcome {
    std::vector<std::string> files, errors, warnings;
};

Outcome walkAll(const std::vector<std::string>& roots, WalkOptions opts) {
    Outcome out;
    opts.warning = [&](const std::string& p, const std::string& msg) {
        out.warnings.push_back(slashes(p) + ": " + msg);
    };
    walk(roots, opts, [&](const std::string& p) { out.files.push_back(slashes(p)); },
         [&](const std::string& p, const std::string& msg) {
             out.errors.push_back(slashes(p) + ": " + msg);
         });
    return out;
}

}  // namespace

TEST_CASE("-R: a link to its own directory is a reported loop, and files are listed once") {
    Tree t;
    mediagrep::test::makeSymlink(".", t.path("a/loop"));
    const Outcome o = walkAll({t.t.root}, following());
    CHECK(o.files.size() == 5);
    CHECK(o.errors.empty());
    REQUIRE(o.warnings.size() == 1);
    CHECK(o.warnings[0] == slashes(t.path("a/loop")) + ": recursive directory loop");
}

TEST_CASE("-R: a link to an ancestor is a loop, even through several links") {
    Tree t;
    mediagrep::test::makeSymlink("..", t.path("a/b/up"));
    mediagrep::test::makeSymlink("../..", t.path("a/c/top"));
    const Outcome o = walkAll({t.t.root}, following());
    CHECK(o.files.size() == 5);
    CHECK(o.errors.empty());
    CHECK(o.warnings.size() == 2);
}

TEST_CASE("-R: the chain starts at a root that is itself a link") {
    Tree t;
    mediagrep::test::makeSymlink("..", t.path("a/up"));
    mediagrep::test::makeSymlink("a", t.path("start"));
    // start is a. a/up leads to the tree root, which is not on the chain yet, so it is
    // entered; there, both a and start lead back to a, which is.
    const Outcome o = walkAll({t.path("start")}, following());
    CHECK(o.errors.empty());
    CHECK(o.warnings == std::vector<std::string>{
        slashes(t.path("start/up/a")) + ": recursive directory loop",
        slashes(t.path("start/up/start")) + ": recursive directory loop"});
    CHECK(o.files.size() == 5);  // a's four files, then top.mp3 through up/
}

TEST_CASE("-R: two links to the same directory (a diamond) are both walked") {
    Tree t;
    mediagrep::test::makeSymlink("../b", t.path("a/c/one"));
    mediagrep::test::makeSymlink("b", t.path("a/two"));
    const Outcome o = walkAll({t.path("a")}, following());
    CHECK(o.warnings.empty());
    CHECK(o.errors.empty());
    // b itself, plus c/one and two: three visits of b's two files.
    CHECK(std::count(o.files.begin(), o.files.end(), slashes(t.path("a/two/2.mp4"))) == 1);
    CHECK(std::count(o.files.begin(), o.files.end(), slashes(t.path("a/c/one/2.mp4"))) == 1);
    CHECK(std::count(o.files.begin(), o.files.end(), slashes(t.path("a/b/2.mp4"))) == 1);
    CHECK(o.files.size() == 2 + 3 * 2);  // a's 1.mkv and z.mkv, then b's two files three times
}

TEST_CASE("-R: a broken link is reported as an error") {
    Tree t;
    mediagrep::test::makeSymlink("nowhere", t.path("a/broken"));
    const Outcome o = walkAll({t.t.root}, following());
    CHECK(o.files.size() == 5);
    REQUIRE(o.errors.size() == 1);
    CHECK(o.errors[0] == slashes(t.path("a/broken")) + ": No such file or directory");
    CHECK(o.warnings.empty());
}

TEST_CASE("-r: links are skipped silently, loops and broken ones alike") {
    Tree t;
    mediagrep::test::makeSymlink(".", t.path("a/loop"));
    mediagrep::test::makeSymlink("nowhere", t.path("a/broken"));
    const Outcome o = walkAll({t.t.root}, recursive());
    CHECK(o.files.size() == 5);
    CHECK(o.errors.empty());
    CHECK(o.warnings.empty());
}

TEST_CASE("fsys::directoryId is equal for a directory and a link to it, and differs otherwise") {
    Tree t;
    mediagrep::test::makeSymlink("a", t.path("link"));
    const auto a = fsys::directoryId(t.path("a"));
    const auto viaLink = fsys::directoryId(t.path("link"));
    const auto b = fsys::directoryId(t.path("a/b"));
    REQUIRE(a);
    REQUIRE(viaLink);
    REQUIRE(b);
    CHECK(*a == *viaLink);
    CHECK_FALSE(*a == *b);
    CHECK_FALSE(fsys::directoryId(t.path("nope")));
}
#endif
