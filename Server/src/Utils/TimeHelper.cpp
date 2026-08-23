#include "TimeHelper.h"
#include <cstdint>
#include <string>
#include <chrono>
namespace Blog{
    std::string TimeHelper::toFormatTime(uint64_t tomeFromEpoch, const std::string& fmt){
                 // 将毫秒转为 time_t（秒）
    std::time_t seconds = tomeFromEpoch / 1000;

    // 转为本地时间结构
    std::tm* tm_local = std::localtime(&seconds);

    // 使用 stringstream 格式化
    std::ostringstream oss;
    oss << std::put_time(tm_local, fmt.c_str());

    return oss.str();   
    }

    uint64_t TimeHelper::getCurrentTimeFromEpochMills(){
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        uint64_t millis = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return millis;
    }

    std::string TimeHelper::toRfc822(uint64_t timeFromEpochMills){
        std::time_t seconds = (std::time_t)(timeFromEpochMills / 1000);
        std::tm* tm_utc = std::gmtime(&seconds);
        std::ostringstream oss;
        oss << std::put_time(tm_utc, "%a, %d %b %Y %H:%M:%S") << " +0800";
        return oss.str();
    }

}