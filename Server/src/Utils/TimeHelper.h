#ifndef TIME_HELPER_H
#define TIME_HELPER_H
#include <cstdint>
#include <string>
namespace Blog{
    class TimeHelper{
        public:
        static std::string toFormatTime(uint64_t timeFromEpochMills, const std::string& fmt = "%Y-%m-%d %H:%M");
        static uint64_t getCurrentTimeFromEpochMills();
        static std::string toRfc822(uint64_t timeFromEpochMills);
    };
}
#endif