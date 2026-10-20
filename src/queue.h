// A blocking FIFO with a capacity, closed by the producer when done.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace mediagrep {

template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity ? capacity : 1) {}

    // Blocks while the queue is full.
    void push(T value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            notFull_.wait(lock, [&] { return items_.size() < capacity_; });
            items_.push_back(std::move(value));
        }
        notEmpty_.notify_one();
    }

    // Blocks until an item is available. Returns nullopt once the queue is
    // closed and empty.
    std::optional<T> pop() {
        std::optional<T> out;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            notEmpty_.wait(lock, [&] { return !items_.empty() || closed_; });
            if (items_.empty()) return std::nullopt;
            out.emplace(std::move(items_.front()));
            items_.pop_front();
        }
        notFull_.notify_one();
        return out;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> items_;
    bool closed_ = false;
};

}  // namespace mediagrep
