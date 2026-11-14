#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace Blog::Utils {
class ThreadPool {
public:
  explicit ThreadPool(size_t n) {
    for (size_t i = 0; i < n; ++i) {
      workers_.emplace_back([this] {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty())
              return;
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        }
      });
    }
  }
  ~ThreadPool() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    cv_.notify_all();
    for (auto &t : workers_)
      t.join();
  }
  void submit(std::function<void()> task) {
    { std::lock_guard<std::mutex> lock(mutex_); tasks_.push(std::move(task)); }
    cv_.notify_one();
  }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

// Shared pool for async image dimension refresh.
inline ThreadPool &imagePool() {
  static ThreadPool pool(2);
  return pool;
}
} // namespace Blog::Utils
