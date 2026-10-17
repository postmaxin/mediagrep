#include <atomic>
#include <chrono>
#include <thread>

#include "doctest.h"
#include "pool.h"

using namespace mediagrep;

namespace {

// Work that finishes out of order on purpose: later jobs are often faster.
int slowDouble(const int& n) {
    std::this_thread::sleep_for(std::chrono::milliseconds((n * 7) % 5));
    return n * 2;
}

}  // namespace

TEST_CASE("results are emitted in submission order despite parallel completion") {
    std::vector<int> out;
    OrderedPool<int, int> pool(4, slowDouble, [&](int&& r) { out.push_back(r); });
    for (int i = 0; i < 50; ++i) pool.submit(i);
    pool.finish();
    REQUIRE(out.size() == 50);
    for (int i = 0; i < 50; ++i) CHECK(out[i] == i * 2);
}

TEST_CASE("a single worker works") {
    std::vector<int> out;
    OrderedPool<int, int> pool(1, slowDouble, [&](int&& r) { out.push_back(r); });
    for (int i = 0; i < 10; ++i) pool.submit(i);
    pool.finish();
    CHECK(out == std::vector<int>{0, 2, 4, 6, 8, 10, 12, 14, 16, 18});
}

TEST_CASE("finishing with no jobs is fine") {
    int emitted = 0;
    OrderedPool<int, int> pool(3, slowDouble, [&](int&&) { ++emitted; });
    pool.finish();
    CHECK(emitted == 0);
}

TEST_CASE("many jobs do not deadlock under backpressure") {
    std::atomic<int> emitted{0};
    OrderedPool<int, int> pool(2, [](const int& n) { return n; }, [&](int&&) { ++emitted; });
    for (int i = 0; i < 5000; ++i) pool.submit(i);
    pool.finish();
    CHECK(emitted == 5000);
}

TEST_CASE("destroying the pool without finish still completes the work") {
    std::vector<int> out;
    {
        OrderedPool<int, int> pool(3, slowDouble, [&](int&& r) { out.push_back(r); });
        for (int i = 0; i < 20; ++i) pool.submit(i);
    }
    CHECK(out.size() == 20);
}

TEST_CASE("emit runs on a single thread at a time") {
    std::atomic<int> concurrent{0};
    std::atomic<bool> overlapped{false};
    OrderedPool<int, int> pool(4, [](const int& n) { return n; }, [&](int&&) {
        if (++concurrent > 1) overlapped = true;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        --concurrent;
    });
    for (int i = 0; i < 200; ++i) pool.submit(i);
    pool.finish();
    CHECK_FALSE(overlapped);
}
