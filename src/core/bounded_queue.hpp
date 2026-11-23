#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace edge {

// Fixed-capacity queue. When full, the oldest item is discarded so a slow
// consumer can never grow memory without bound.
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  ~BoundedQueue() { close(); }

  // Returns true when an older item was dropped to make room. Returns false
  // when the queue has been closed.
  bool push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return false;
    }
    bool dropped = false;
    if (items_.size() >= capacity_) {
      items_.pop_front();
      ++dropped_;
      dropped = true;
    }
    items_.push_back(std::move(value));
    not_empty_.notify_one();
    return dropped;
  }

  // Blocks until an item is available or the queue is closed and drained.
  bool pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !items_.empty() || closed_; });
    if (items_.empty()) {
      return false;
    }
    out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
  }

  std::size_t dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

  std::size_t capacity() const { return capacity_; }

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<T> items_;
  const std::size_t capacity_;
  std::size_t dropped_ = 0;
  bool closed_ = false;
};

}  // namespace edge
