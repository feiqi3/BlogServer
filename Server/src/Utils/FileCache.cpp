#include "FileCache.h"
#include "FDef.h"
#include "Utils/FileReader.h"
#include "oneapi/tbb/concurrent_map.h"
#include "tbb/concurrent_map.h"
#include <memory>
#include <mutex>
#include <string>

namespace {
uint64_t getTime() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
} // namespace

namespace Blog {

struct FileCacheLine {
  MemMapedFilePtr file;
  uint64_t cacheTime;
};
using CacheMap = tbb::concurrent_map<std::string, FileCacheLine>;
class FileCacheInner {
public:
  CacheMap mmap;
  std::mutex mlock;
};

FileCache::FileCache(uint64_t maxCacheTime):dp(new FileCacheInner),cacheOutDateTime(maxCacheTime){

}

void FileCache::invalid(const std::string &path) {
  auto &l = dp->mlock;
  FAUTO_LOCK(l);
  dp->mmap.unsafe_erase(path);
}

MemMapedFilePtr FileCache::getOrGen(const std::string &path) const {
  {
    auto itor = dp->mmap.find(path);
    if (itor != dp->mmap.end()) {
      itor->second.cacheTime = getTime();
      return itor->second.file;
    }
  }
  FileCacheLine line{
      .file = std::make_shared<MemoryMappedFile>(path, Mode::ReadOnly, 0),
      .cacheTime = getTime()};
  dp->mmap.insert({path, line});
  return line.file;
}
uint32_t FileCache::size() const { return dp->mmap.size(); }

void FileCache::checkOverdue() {
  auto time = getTime();
  for (auto itor = dp->mmap.begin(); itor != dp->mmap.end();) {
    if (itor->second.cacheTime - time > this->cacheOutDateTime) {
      auto &l = dp->mlock;
      FAUTO_LOCK(l);
      itor = dp->mmap.unsafe_erase(itor);
    } else {
      itor++;
    }
  }
}
} // namespace Blog