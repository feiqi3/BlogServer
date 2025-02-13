#ifndef F_DEF_H
#define F_DEF_H
#include "errno.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <cstdint>

#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning( once : 4251 )
#endif
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

#ifdef _WIN32
#define EPOLL_EVENT_STRUCT_ALIGN
#elif defined(__linux__) or defined(__APPLE__)
#define EPOLL_EVENT_STRUCT_ALIGN __attribute__((__packed__))
#endif

namespace Fei {
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
using int64 = int64_t;
using Byte = uint8;

#if defined(__linux__) or defined(__APPLE__)
using EpollHandle = int;
using Socket = int;
#else
using EpollHandle = void *;
using Socket = uint64;
#endif
using Event = uint32;
using AtomicEvent = std::atomic_uint32_t;

typedef union FEpollData {
  void *ptr;
  int i32;
  uint32_t u32;
  uint64_t u64;
  Socket sock;
} epoll_data_t;

struct F_API FEpollEvent {
  uint32 events;
  FEpollData data;
}EPOLL_EVENT_STRUCT_ALIGN;

struct F_API FSocketAddr {
  // Impl in Socket.cpp
  FSocketAddr(const char *ip, uint16 port);
  FSocketAddr() = default;

// get port in proper order.
  uint16 getPort()const;

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

using FEventPtr = std::shared_ptr<class FEvent>;
using FEventPtrWeak= std::weak_ptr<class FEvent>;
}; // namespace Fei


#endif