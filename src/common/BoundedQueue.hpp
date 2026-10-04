#pragma once
// BoundedQueue<T> - thread-safe producer/consumer queue with a fixed capacity.
//
// push() blocks while the queue is full, pop() blocks while it is empty.
// After close(), push() fails and pop() drains the remaining items, then fails.
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace bb {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : cap_(capacity ? capacity : 1) {}

    // Returns false if the queue was closed.
    bool push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        notFull_.wait(lk, [&] { return closed_ || q_.size() < cap_; });
        if (closed_) return false;
        q_.push_back(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // Returns false once the queue is closed AND empty.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        notEmpty_.wait(lk, [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        notFull_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

private:
    mutable std::mutex m_;
    std::condition_variable notEmpty_, notFull_;
    std::deque<T> q_;
    std::size_t cap_;
    bool closed_ = false;
};

}  // namespace bb
