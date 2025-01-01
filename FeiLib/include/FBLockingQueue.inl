#ifndef FBLOCKING_QUEUE_INL
#define FBLOCKING_QUEUE_INL
#include "FBlockingQueue.h"

namespace Fei{
  //BoundedBlockingQueue
template <typename T> void BoundedBlockingQueue<T>::put(const T &task) {
  std::unique_lock<std::mutex> lock(mtx);
  while (queue_.size() == capacity_) {
    full_.wait(lock);
  }
  assert(queue_.size() < capacity_);
  queue_.push(task);
  empty_.notify_all();
}

template <typename T> T BoundedBlockingQueue<T>::take() {
  std::unique_lock<std::mutex> lock(mtx);
  while (queue_.empty()) {
    empty_.wait(lock);
  }
  assert(!queue_.empty());
  T front(std::move(queue_.front()));
  queue_.pop();
  full_.notify_all();
  return front;
}

template <typename T> size_t BoundedBlockingQueue<T>::size() {
  std::lock_guard<std::mutex> lock(mtx);
  return queue_.size();
}

template <typename T> bool BoundedBlockingQueue<T>::empty() {
  std::unique_lock<std::mutex> lock(mtx);
  return queue_.empty();
}

template <typename T> void BoundedBlockingQueue<T>::put(T &&task) {
  std::unique_lock<std::mutex> lock(mtx);
  while (queue_.size() == capacity_) {
    full_.wait(lock);
  }
  assert(queue_.size() < capacity_);
  queue_.push(std::move(task));
  empty_.notify_all();
}

//---------------------------BlockingQueue-----------------------------------//

template <typename T> void BlockingQueue<T>::put(const T &task) {
  std::unique_lock<std::mutex> lock(mtx);
  queue_.push(task);
  empty_.notify_all();
}

template <typename T> void BlockingQueue<T>::put(T &&task) {
  std::unique_lock<std::mutex> lock(mtx);
  queue_.push(std::move(task));
  empty_.notify_all();
}

template <typename T> T BlockingQueue<T>::take() {
  std::unique_lock<std::mutex> lock(mtx);
  while (queue_.empty()) {
    empty_.wait(lock);
  }
  assert(!queue_.empty());
  T front(std::move(queue_.front()));
  queue_.pop();
  return front;
}

template <typename T> size_t BlockingQueue<T>::size(){
    std::lock_guard<std::mutex> lock(mtx);
    return queue_.size();
}

template <typename T> bool BlockingQueue<T>::empty(){
    std::lock_guard<std::mutex> lock(mtx);
    return queue_.empty();
}
}

#endif