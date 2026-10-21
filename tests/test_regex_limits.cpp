// Regex matching over very long field values. libstdc++'s default std::regex
// executor recurses once per character, so a long enough value overflows the
// thread's stack; mediagrep uses its non-recursive __polynomial executor
// where it can, and a length limit for patterns that executor rejects
// (back-references). MSVC's gives up with regex_error. None of these may
// crash mediagrep or turn into a per-file error.
#include <string>

#include "doctest.h"
#include "eval.h"
#include "fake_source.h"
#include "parser.h"
#include "pool.h"
#include "thread.h"

using namespace mediagrep;
using namespace mediagrep::test;

namespace {

Match run(std::string_view text, const MetadataSource& src) {
    ExprPtr expr = parse(text);
    Evaluator ev(*expr, EvalOptions{});
    return ev.evaluate(src);
}

struct LimitGuard {
    std::size_t saved = threadRegexLimit();
    explicit LimitGuard(std::size_t limit) { setThreadRegexLimit(limit); }
    ~LimitGuard() { setThreadRegexLimit(saved); }
};

}  // namespace

TEST_CASE("values longer than the thread's regex limit are skipped, with a warning") {
    const LimitGuard guard(100);
    FakeSource src;
    src.add(TrackKind::General, {text("Description", std::string(200, 'a') + " zzz"),
                                 text("Title", "short zzz")});

    // Back-references need the executor that is limited by length. (The
    // parser unescapes a quoted string, so '\\1' reaches the regex as \1.)
    const std::string zz = "'(z)\\\\1'";
    const Match m = run("Description ~ " + zz, src);
    CHECK_FALSE(m.matched);
    REQUIRE(m.warnings.size() == 1);
    CHECK(m.warnings[0].find("longer than 100 characters") != std::string::npos);

    // A skipped value has no answer, so it does not satisfy !~ either.
    CHECK_FALSE(run("Description !~ '(q)\\\\1'", src).matched);

    // Bare patterns still see the short fields.
    const Match p = run(zz, src);
    CHECK(p.matched);
    REQUIRE(p.fields.size() == 1);
    CHECK(p.fields[0].name == "Title");

    // Equality is not a regex and sees the whole value.
    CHECK(run("Description = '" + std::string(200, 'a') + " zzz'", src).matched);
}

TEST_CASE("pool workers run backtracking regexes over long values without crashing") {
    FakeSource src;
    // The match ends the run of a's, so both patterns recurse once per
    // character but finish in linear time from the first start position.
    src.add(TrackKind::General, {text("Description", std::string(200000, 'a') + "zzz")});
    for (const char* text : {"Description ~ 'a.*zzz'", "Description ~ '(a|b)*zzz'"}) {
        CAPTURE(text);
        const ExprPtr expr = parse(text);
        const Evaluator ev(*expr, EvalOptions{});
        Match result;
        {
            OrderedPool<int, Match> pool(
                1, [&](const int&) { return ev.evaluate(src); },
                [&](Match&& m) { result = std::move(m); });
            pool.submit(0);
            pool.finish();
        }
        // Either the engine answered, or it gave up and said so.
        CHECK((result.matched || !result.warnings.empty()));
    }
}

TEST_CASE("deeply nested groups over a long value do not crash a worker") {
    // About 10 KB of stack per character with the recursive executor: this
    // overflowed even the 256 MB worker stack (issue #7).
    std::string pattern(32, '(');
    pattern += 'a';
    pattern += std::string(32, ')');
    pattern += "*b";
    FakeSource src;
    src.add(TrackKind::General, {text("Description", std::string(200000, 'a') + "b")});
    const ExprPtr expr = parse("Description ~ '" + pattern + "'");
    const Evaluator ev(*expr, EvalOptions{});
    Match result;
    {
        OrderedPool<int, Match> pool(
            1, [&](const int&) { return ev.evaluate(src); },
            [&](Match&& m) { result = std::move(m); });
        pool.submit(0);
        pool.finish();
    }
#if defined(__GLIBCXX__)
    CHECK(result.matched);
    CHECK(result.warnings.empty());
#else
    CHECK((result.matched || !result.warnings.empty()));
#endif
}

#if defined(__GLIBCXX__)
TEST_CASE("patterns the polynomial executor accepts are not subject to the length limit") {
    const LimitGuard guard(100);
    FakeSource src;
    src.add(TrackKind::General, {text("Description", std::string(5000, 'a') + " zzz")});
    const Match m = run("Description ~ 'a.*zzz'", src);
    CHECK(m.matched);
    CHECK(m.warnings.empty());
    // A back-reference pattern still works on a value within the limit.
    const LimitGuard inner(100000);
    CHECK(run("Description ~ '(z)\\\\1'", src).matched);
}
#endif

TEST_CASE("worker threads get a regex limit sized to their stack") {
    std::size_t limit = 0;
    WorkerThread t([&] { limit = threadRegexLimit(); });
    t.join();
    // 200,000 characters must be within reach on every platform.
    CHECK(limit >= 200000);
}
