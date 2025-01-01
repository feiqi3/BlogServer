#ifndef FBLOCKING_QUEUE_H
#define FBLOCKING_QUEUE_H


#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include "FDef.h"
#include "FNoCopyable.h"

namespace Fei {

template <typename T> class BoundedBlockingQueue: public FNoCopyable {
public:
  BoundedBlockingQueue(uint32 maxCap)
      : mtx(), full_(), empty_(), capacity_(maxCap) {}
  void put(const T &t);
  void put(T &&t);
  T take();
  size_t size();
  bool empty();

private:
  mutable std::mutex mtx;
  std::condition_variable full_;
  std::condition_variable empty_;
  std::queue<T> queue_;
  size_t capacity_;
};

template <typename T> class BlockingQueue : public FNoCopyable {
public:
  BlockingQueue() : mtx(), full_(), empty_() {}
  void put(const T &t);
  void put(T &&t);
  T take();
  size_t size();
  bool empty();

private:
  mutable std::mutex mtx;
  std::condition_variable full_;
  std::condition_variable empty_;
  std::queue<T> queue_;
};

} // namespace Fei

#include "FBLockingQueue.inl"
#endif
