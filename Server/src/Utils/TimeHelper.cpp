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
}