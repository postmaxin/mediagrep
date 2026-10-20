// Worker threads with a stack big enough for std::regex, and the length
// of field value each thread can safely hand to std::regex_search.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace mediagrep {

// A thread that asks for a large stack (256 MB of address space, falling
// back to smaller sizes) where the platform allows it. Must be joined.
class WorkerThread {
public:
    explicit WorkerThread(std::function<void()> fn);
    ~WorkerThread();
    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    void join();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The longest value this thread may pass to std::regex_search. libstdc++
// recurses once per character on patterns such as 'a.*b', so the limit
// follows the thread's stack there; other libraries have no limit.
std::size_t threadRegexLimit();
void setThreadRegexLimit(std::size_t chars);

}  // namespace mediagrep
