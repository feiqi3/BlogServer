#pragma once   
#include "Utils/FileReader.h"
#include <cstdint>
#include <memory>
#include <string>
namespace Blog{
    class FileCache{
        public:
            FileCache(uint64_t maxCacheTimeInMs);
            MemMapedFilePtr getOrGen(const std::string& path)const;
            void invalid(const std::string& path);
            uint32_t size()const;
            void checkOverdue();
        private:
            std::unique_ptr<class FileCacheInner> dp= 0;
            uint64_t cacheOutDateTime;
    };    
};