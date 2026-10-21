// A worker pool that runs jobs in parallel but delivers results in
// submission order, so output is deterministic regardless of timing.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "thread.h"

namespace mediagrep {

template <class Job, class Result>
class OrderedPool {
public:
    using Work = std::function<Result(const Job&)>;   // runs on a worker thread; must not throw
    using Emit = std::function<void(Result&&)>;       // runs on one output thread, in order
    // Tests only: called with the index of each thread (workers first, then
    // the output thread) just before it starts. Throwing makes that start fail.
    using StartHook = std::function<void(std::size_t)>;

    OrderedPool(std::size_t workers, Work work, Emit emit, StartHook beforeStart = nullptr)
        : work_(std::move(work)), emit_(std::move(emit)) {
        if (workers == 0) workers = 1;
        capacity_ = workers * 4;
        // Reserve first so push_back cannot throw once a thread is running.
        workers_.reserve(workers);
        try {
            for (std::size_t i = 0; i < workers; ++i) {
                if (beforeStart) beforeStart(i);
                workers_.push_back(std::make_unique<WorkerThread>([this] { workerLoop(); }));
            }
            if (beforeStart) beforeStart(workers);
            output_ = std::thread([this] { outputLoop(); });
        } catch (...) {
            // A constructor that throws gets no destructor call. The workers
            // already started are waiting for jobs, so tell them to stop or
            // joining them (in ~WorkerThread) would never return.
            stopWorkers();
            throw;
        }
    }

    ~OrderedPool() { finish(); }
    OrderedPool(const OrderedPool&) = delete;
    OrderedPool& operator=(const OrderedPool&) = delete;

    // Queues a job. Blocks when too many jobs are in flight, to bound memory.
    void submit(Job job) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            notFull_.wait(lock, [&] { return nextSeq_ - emittedSeq_ < capacity_; });
            jobs_.emplace_back(nextSeq_++, std::move(job));
        }
        jobReady_.notify_one();
    }

    // Waits until every submitted job has been emitted. Idempotent.
    void finish() {
        if (!stopWorkers()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            workersDone_ = true;
        }
        resultReady_.notify_all();
        output_.join();
    }

private:
    // Marks the pool finished, wakes the workers and joins them. Returns false
    // if it had already been done.
    bool stopWorkers() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) return false;
            finished_ = true;
        }
        jobReady_.notify_all();
        for (auto& t : workers_) t->join();
        return true;
    }

    void workerLoop() {
        for (;;) {
            std::pair<std::size_t, Job> item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                jobReady_.wait(lock, [&] { return !jobs_.empty() || finished_; });
                if (jobs_.empty()) return;
                item = std::move(jobs_.front());
                jobs_.pop_front();
            }
            Result result = work_(item.second);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                results_.emplace(item.first, std::move(result));
            }
            resultReady_.notify_all();
        }
    }

    void outputLoop() {
        for (;;) {
            std::optional<Result> result;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                resultReady_.wait(lock, [&] {
                    return results_.count(emittedSeq_) != 0 || (workersDone_ && results_.empty());
                });
                auto it = results_.find(emittedSeq_);
                if (it == results_.end()) return;  // workers done and nothing left
                result.emplace(std::move(it->second));
                results_.erase(it);
                ++emittedSeq_;
            }
            notFull_.notify_all();
            emit_(std::move(*result));
        }
    }

    Work work_;
    Emit emit_;
    std::size_t capacity_ = 4;

    std::mutex mutex_;
    std::condition_variable jobReady_;     // workers wait for jobs
    std::condition_variable resultReady_;  // output thread waits for the next result
    std::condition_variable notFull_;      // submit waits for room
    std::deque<std::pair<std::size_t, Job>> jobs_;
    std::map<std::size_t, Result> results_;
    std::size_t nextSeq_ = 0;
    std::size_t emittedSeq_ = 0;
    bool finished_ = false;
    bool workersDone_ = false;

    std::vector<std::unique_ptr<WorkerThread>> workers_;  // big stacks, for std::regex
    std::thread output_;
};

}  // namespace mediagrep
