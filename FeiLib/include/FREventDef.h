#ifndef FREVENT_H
#define FREVENT_H
#include "FDef.h"

namespace Fei {
namespace REvent {
enum EventEnum {
  Rdnorm = 1 << 0,
  Rdband = 1 << 1,
  Pri = 1 << 2,
  Wrnorm = 1 << 3,
  Wrband = 1 << 4,
  Err = 1 << 5,
  Hup = 1 << 6,
  Rdhup = 1 << 7,
  // Nval --> poll specific
  Nval = 1 << 8,

  In = 1 << 9,
  Out = 1 << 10,

  // Epoll's specific events
  Msg = 1 << 11,
  Oneshot = 1 << 12,

  MAX_ENUM = Oneshot,

};

F_API const char *REventToString(EventEnum events);

F_API uint32 ToEpoll(uint32 events);
F_API uint32 FromEpoll(uint32 events);
F_API uint32 ToPoll(uint32 events);
F_API uint32 FromPoll(uint32 events);

}; // namespace REvent
}; // namespace Fei
#endif