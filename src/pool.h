// A worker pool that runs jobs in parallel but delivers results in
// submission order, so output is deterministic regardless of timing.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace mediagrep {

template <class Job, class Result>
class OrderedPool {
public:
    using Work = std::function<Result(const Job&)>;   // runs on a worker thread; must not throw
    using Emit = std::function<void(Result&&)>;       // runs on one output thread, in order

    OrderedPool(std::size_t workers, Work work, Emit emit)
        : work_(std::move(work)), emit_(std::move(emit)) {
        if (workers == 0) workers = 1;
        capacity_ = workers * 4;
        for (std::size_t i = 0; i < workers; ++i)
            workers_.emplace_back([this] { workerLoop(); });
        output_ = std::thread([this] { outputLoop(); });
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) return;
            finished_ = true;
        }
        jobReady_.notify_all();
        for (auto& t : workers_) t.join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            workersDone_ = true;
        }
        resultReady_.notify_all();
        output_.join();
    }

private:
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

    std::vector<std::thread> workers_;
    std::thread output_;
};

}  // namespace mediagrep
