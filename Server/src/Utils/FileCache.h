#pragma once   
#include "Utils/FileReader.h"
#include <cstdint>
#include <memory>
#include <string>
#include "Utils/Singleton.h"
namespace Blog{
    class FileCache:public Singleton<FileCache>{
        public:
            FileCache(uint64_t maxCacheTimeInMs);
            ~FileCache();
            MemMapedFilePtr getOrGen(const std::string& path)const;
            void invalid(const std::string& path);
            uint32_t size()const;
            void checkOverdue(uint64_t time_ms);
        private:
            class FileCacheInner* dp= 0;
            uint64_t cacheOutDateTime;
    };    
};