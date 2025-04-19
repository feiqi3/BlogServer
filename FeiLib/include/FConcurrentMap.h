#ifndef FCONCURRENTMAP_H
#define FCONCURRENTMAP_H

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_map.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>

namespace Fei {

// A concurrent map implementation using TBB's concurrent_hash_map
// This class provides thread-safe read and write operations
template <typename Key, typename Value> class FConcurrentHashMap {
public:
  FConcurrentHashMap() = default;
  FConcurrentHashMap(const FConcurrentHashMap &) = delete;
  FConcurrentHashMap &operator=(const FConcurrentHashMap &) = delete;

  bool findAndModifyLocked(const Key &key, std::function<void(Value &)> func) {
    tryLockRead();
    bool isFind = false;
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      func(acc->second);
      acc.release();
      isFind = true;
    }
    tryUnlockRead();
    return isFind;
  }

  // Not Safe.
  bool find(const Key &key, Value &val) {
    tryLockRead();
    bool isFind = false;
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      val = acc->second;
      acc.release();
      isFind = true;
    }
    tryUnlockRead();
    return isFind;
  }

  bool find(const Key &key) const {
    tryLockRead();
    bool isFind = false;
    typename tbb::concurrent_hash_map<Key, Value>::const_accessor acc;
    if (mMap.find(acc, key)) {
      acc.release();
      isFind = true;
    }
    tryUnlockRead();
    return isFind;
  }

  void traversal(std::function<void(const Key &, Value &)> func) {
    tryLockRead();
    for (auto it = mMap.begin(); it != mMap.end(); ++it) {
      typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
      {
        if (mMap.find(acc, it->first)) {
          // Write lock
          func(it->first, it->second);
          acc.release();
        }
      }
    }
    tryUnlockRead();
  }

  void traversal(std::function<void(const Key &, const Value &)> func) const {
    tryLockRead();
    for (auto it = mMap.cbegin(); it != mMap.cend(); ++it) {
      typename tbb::concurrent_hash_map<Key, Value>::const_accessor acc;
      {
        if (mMap.find(acc, it->first)) {
          // Write lock
          func(it->first, it->second);
          acc.release();
        }
      }
    }
    tryUnlockRead();
  }

  void insert(const Key &key, const Value &val) {
    tryLockWrite();
    mMap.insert({key, val});
    tryUnlockWrite();
  }

  void erase(const Key &key) {
    tryLockWrite();
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      mMap.erase(acc);
    }
    tryUnlockWrite();
  }

  void clear() {
    tryLockWrite();
    mMap.clear();
    tryUnlockWrite();
  }

private:
  inline void ReadCheck() { assert(mReadingCounts >= 0); }
  inline void WriteCheck() { assert(mWriteLock >= 0); }

  void tryLockRead() {
    while (1) {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      if (mWriteLock == 0) {
        mReadingCounts++;
        return;
      }
    }
  }

  void tryUnlockRead() {
    {
      mReadingCounts--;
    }
    ReadCheck();
  }

  void tryLockWrite() {
    {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      mWriteLock++;
    }
    while (mReadingCounts > 0) ;//spin wait
    mModifyMutex.lock();
  }

  void tryUnlockWrite() {
    mModifyMutex.unlock();
    {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      mWriteLock--;
    }
    WriteCheck();
  }
  std::mutex mWriteMutex;
  std::mutex mModifyMutex;

  tbb::concurrent_hash_map<Key, Value> mMap;

  mutable std::atomic_int mReadingCounts = 0;
  std::atomic_int mWriteLock = 0;
};

// According to oneTbb specification, concurrent_map's iterator is "thread safe"
// member funciton.
template <typename Key, typename Value, typename Comp> class FConcurrentMap {
public:
  FConcurrentMap() = default;
  FConcurrentMap(const FConcurrentMap &) = delete;
  FConcurrentMap &operator=(const FConcurrentMap &) = delete;

  bool findAndModifyLocked(const Key &key, std::function<void(Value &)> func) {
    tryLockWrite();
    auto itor = mMap.find(key);
    bool isFind = false;
    if (itor != mMap.end()) {
      func(itor->second);
      isFind = true;
    }
    tryUnlockWrite();
    return isFind;
  }

  bool find(const Key &key, Value &val) {
    tryLockRead();
    bool isFind= false;
    auto itor = mMap.find(key);
    if (itor != mMap.end()) {
      val = itor->second;
      isFind = true;
    }
    tryUnlockRead();
    return isFind;
  }

  bool find(const Key &key) const {
    tryLockRead();
    bool isFind = false;

    if (mMap.find(key) != mMap.end()) {
      isFind = true;
    }
    tryUnlockRead();
    return isFind;
  }

  void traversal(std::function<void(const Key &, Value &)> func) {
    tryLockRead();
    for (auto it = mMap.begin(); it != mMap.end(); ++it) {
      {
        func(it->first, it->second);
      }
    }
    tryUnlockRead();
  }

  //Concurrent map's insert is thread safe
  void insert(const Key &key, const Value &val) {
    tryLockRead();
    mMap.insert({key, val});
    tryUnlockRead();
  }

  void erase(const Key &key) {
    tryLockWrite();

    auto itor = mMap.find(key);
    if (itor != mMap.end()) {
      mMap.unsafe_erase(itor);
    }

    tryUnlockWrite();
  }

  void clear() {
    tryLockWrite();

    mMap.clear();

    tryUnlockWrite();
  }

private:
  inline void ReadCheck() { assert(mReadingCounts >= 0); }
  inline void WriteCheck() { assert(mWriteLock >= 0); }

  void tryLockRead() {
    while (1) {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      if (mWriteLock == 0) {
        mReadingCounts++;
        return;
      }
    }
  }

  void tryUnlockRead() {
    {
      mReadingCounts--;
    }
    ReadCheck();
  }

  void tryLockWrite() {
    {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      mWriteLock++;
    }
    while (mReadingCounts > 0) ;//spin wait
    mModifyMutex.lock();
  }

  void tryUnlockWrite() {
    mModifyMutex.unlock();
    {
      std::lock_guard<std::mutex> lock(mWriteMutex);
      mWriteLock--;
    }
    WriteCheck();
  }
  std::mutex mWriteMutex;
  std::mutex mModifyMutex;
  mutable std::atomic_int mReadingCounts = 0;
  std::atomic_int mWriteLock = 0;
  
  tbb::concurrent_map<Key, Value, Comp> mMap;


  

};
} // namespace Fei

#endif
