#ifndef FCONCURRENTMAP_H
#define FCONCURRENTMAP_H

#include <atomic>
#include <cassert>
#include <functional>
#include "tbb/concurrent_hash_map.h"

namespace Fei {

// A concurrent map implementation using TBB's concurrent_hash_map
// This class provides thread-safe read and write operations
template <typename Key, typename Value> class FConcurrentHashMap {
public:
  FConcurrentHashMap() = default;
  FConcurrentHashMap(const FConcurrentHashMap &) = delete;
  FConcurrentHashMap &operator=(const FConcurrentHashMap &) = delete;

  bool findAndModifyLocked(const Key &key, std::function<void(Value&)> func) {
    mReadingCounts++;
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      func(acc->second);
      acc.release();
      mReadingCounts--;
      return true;
    }
    mReadingCounts--;
    return false;
  }

  //Not Safe.
  bool find(const Key &key, Value &val) {
    mReadingCounts++;
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      val = acc->second;
      acc.release();
      mReadingCounts--;
      return true;
    }
    mReadingCounts--;
    return false;
  }

  bool find(const Key& key)const{
    mReadingCounts++;
    typename tbb::concurrent_hash_map<Key, Value>::const_accessor acc;
    if (mMap.find(acc, key)) {
      acc.release();
      mReadingCounts--;
      return true;
    }
    mReadingCounts--;
    return false;
  }

  void traversal(std::function<void(const Key &, Value &)> func) {
    while (mWriteLock != 0)
      ; // Spin wait
    mReadingCounts++;
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
    mReadingCounts--;
    ReadCheck();
  }

  void traversal(std::function<void(const Key &, const Value &)> func) const {
    while (mWriteLock != 0)
      ; // Spin wait
    mReadingCounts++;
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
    mReadingCounts--;
    ReadCheck();
  }

  void insert(const Key &key, const Value &val) {
    mWriteLock++;
    while (mReadingCounts > 0)
      ; // spin wait
    mMap.insert({key, val});
    mWriteLock--;
    WriteCheck();
  }

  void erase(const Key &key) {
    mWriteLock++;
    while (mReadingCounts > 0)
      ; // spin wait
    typename tbb::concurrent_hash_map<Key, Value>::accessor acc;
    if (mMap.find(acc, key)) {
      mMap.erase(acc);
    }
    mWriteLock--;
    WriteCheck();
  }

  void clear( ) {
    mWriteLock++;
    while (mReadingCounts > 0)
      ; // spin wait

    mMap.clear();
    mWriteLock--;
    WriteCheck();
  }


private:
  inline void ReadCheck() { assert(mReadingCounts >= 0); }
  inline void WriteCheck() { assert(mWriteLock >= 0); }

  tbb::concurrent_hash_map<Key, Value> mMap;

  mutable std::atomic_int mReadingCounts = 0;
  std::atomic_int mWriteLock = 0;
};
} // namespace Fei

#endif
