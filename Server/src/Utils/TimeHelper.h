#ifndef TIME_HELPER_H
#define TIME_HELPER_H
#include <cstdint>
#include <string>
namespace Blog{
    class TimeHelper{
        static std::string toFormatTime(uint64_t timeFromEpochMills, const std::string& fmt = "%Y-%m-%d %H:%M");
    };
}
#endif