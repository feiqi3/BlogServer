#ifndef F_DEF_H
#define F_DEF_H
#include <cstdint>
#include <mutex>

namespace Fei {
    using uint8 = uint8_t;
    using uint32 = uint32_t;
    using uint64 = uint64_t;
    using int32 = int32_t;
    using int64 = int64_t;
};

#ifdef _WIN32
    #ifdef _F_EXPORT
        #define F_API __declspec(dllexport)
    #else
        #define F_API __declspec(dllimport)
    #endif
#elif defined(__linux__) or defined(__APPLE__)
    #ifdef _F_EXPORT
        #define F_API __attribute__((visibility("default")))
    #else
        #define F_API
    #endif
#endif

#define FAUTO_LOCK(_mutex) std::lock_guard<std::mutex> lock##_mutex(_mutex)

#endif