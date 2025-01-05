#ifndef F_DEF_H
#define F_DEF_H
#include "errno.h"
#include <corecrt.h>
#include <cstdint>
#include <mutex>


namespace Fei {
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
using int64 = int64_t;

using Socket = uint64;
using Event = uint32;
using EpollHandle = void *;

typedef union FEpollData {
  void *ptr;
  int i32;
  uint32_t u32;
  uint64_t u64;
  Socket sock;
} epoll_data_t;

struct FEpollEvent {
  uint32 events;
  FEpollData data;
};

struct FSocketAddr {
  // Impl in Socket.cpp
  FSocketAddr(const char *ip, uint16 port);
  FSocketAddr() = default;
  union {
    struct {
      uint8 a0;
      uint8 a1;
      uint8 a2;
      uint8 a3;
    } un_byte;
    uint32 un_addr;
  } un;
  uint16 port;

  void toHumanFriendyType(char *buf, uint32 len, uint16 *port);
};

#ifdef _WIN32
  using Errno_t = errno_t;
#else
  using Errno_t = int;

#endif

}; // namespace Fei

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