#include <atomic>
#include <cerrno>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include "doctest.h"
#include "pool.h"
#include "queue.h"

using namespace mediagrep;

namespace {

// Work that finishes out of order on purpose: later jobs are often faster.
int slowDouble(const int& n) {
    std::this_thread::sleep_for(std::chrono::milliseconds((n * 7) % 5));
    return n * 2;
}

// Builds a pool whose start hook throws for thread `failAt`, on a helper thread
// so that a constructor which hangs fails the test instead of the whole run.
// Returns what the constructor did: "threw", "built" or "hung".
std::string startWithFailureAt(std::size_t workers, std::size_t failAt) {
    auto outcome = std::make_shared<std::promise<std::string>>();
    std::future<std::string> answer = outcome->get_future();
    std::thread([outcome, workers, failAt] {
        try {
            OrderedPool<int, int> pool(
                workers, slowDouble, [](int&&) {},
                [failAt](std::size_t i) {
                    if (i == failAt)
                        throw std::system_error(EAGAIN, std::generic_category(), "no thread");
                });
            outcome->set_value("built");
        } catch (const std::system_error&) {
            outcome->set_value("threw");
        }
    }).detach();
    if (answer.wait_for(std::chrono::seconds(10)) != std::future_status::ready) return "hung";
    return answer.get();
}

}  // namespace

TEST_CASE("a worker that fails to start makes the constructor throw, not hang") {
    CHECK(startWithFailureAt(4, 2) == "threw");
}

TEST_CASE("the first worker failing to start throws") {
    CHECK(startWithFailureAt(3, 0) == "threw");
}

TEST_CASE("the output thread failing to start throws after the workers began") {
    CHECK(startWithFailureAt(3, 3) == "threw");
}

TEST_CASE("a start hook that never throws leaves the pool working") {
    CHECK(startWithFailureAt(3, 99) == "built");
}

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

TEST_CASE("bounded queue: FIFO, and pop drains before reporting closed") {
    BoundedQueue<int> q(4);
    q.push(1);
    q.push(2);
    q.close();
    CHECK(q.pop() == std::optional<int>(1));
    CHECK(q.pop() == std::optional<int>(2));
    CHECK_FALSE(q.pop().has_value());
}

TEST_CASE("bounded queue: push blocks at capacity until a pop makes room") {
    BoundedQueue<int> q(2);
    std::atomic<int> pushed{0};
    std::thread producer([&] {
        for (int i = 0; i < 5; ++i) {
            q.push(i);
            ++pushed;
        }
        q.close();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(pushed.load() == 2);
    int expected = 0;
    while (auto v = q.pop()) CHECK(*v == expected++);
    producer.join();
    CHECK(expected == 5);
}
