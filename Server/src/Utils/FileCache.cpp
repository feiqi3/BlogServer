#include "FileCache.h"
#include "FDef.h"
#include "Utils/FileReader.h"
#include "FConcurrentMap.h"
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
#include <vector>

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
using CacheMap = Fei::FConcurrentHashMap<std::string, FileCacheLine>;
class FileCacheInner {
public:
  CacheMap mmap;
  std::mutex mlock;
};

FileCache::FileCache(uint64_t maxCacheTime):dp(new FileCacheInner),cacheOutDateTime(maxCacheTime){

}

FileCache::~FileCache(){
  delete dp;
  dp = 0;
}


void FileCache::invalid(const std::string &path) {
    dp->mmap.erase(path);
    Fei::Logger::instance()->log(Fei::lvl::info, "Try erase invalid file cache {}.", path);
}

MemMapedFilePtr FileCache::getOrGen(const std::string &path) const {
  {
    MemMapedFilePtr ret = 0;
    auto isFind = dp->mmap.findAndModifyLocked(path, [&](FileCacheLine &line) {
      line.cacheTime = getTime();
      ret = line.file;
    });
    if(isFind){
      return ret;
    }
  }
  FileCacheLine line{
      .file = std::make_shared<MemoryMappedFile>(path, Mode::ReadOnly, 0),
      .cacheTime = getTime()};
  dp->mmap.insert(path, line);
  return line.file;
}
uint32_t FileCache::size() const { return dp->mmap.size(); }

void FileCache::checkOverdue(uint64_t time_ms) {
  std::vector<std::string> toErase;
  dp->mmap.traversal([&](const std::string &key,const FileCacheLine &line) {
    if (time_ms - line.cacheTime > this->cacheOutDateTime) {
      toErase.push_back(key);
    }
    return true;
  });

  for (const auto &key : toErase) {
      Fei::Logger::instance()->log(Fei::lvl::info, "Try erase invalid file cache {}.", key);
      dp->mmap.erase(key);
  }
}
} // namespace Blog