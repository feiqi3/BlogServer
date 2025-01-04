#include "FREventDef.h"
#include "FSocket.h"

namespace Fei {
namespace REvent {
uint32 ToEpoll(uint32 events) {
  uint32 epollEvents = 0;
  if (events & Rdnorm) {
    epollEvents |= EPOLLRDNORM;
  }
  if (events & Rdband) {
    epollEvents |= EPOLLRDBAND;
  }
  if (events & In) {
    epollEvents |= EPOLLIN;
  }
  if (events & Out) {
    epollEvents |= EPOLLOUT;
  }
  if (events & Pri) {
    epollEvents |= EPOLLPRI;
  }
  if (events & Wrnorm) {
    epollEvents |= EPOLLWRNORM;
  }
  if (events & Wrband) {
    epollEvents |= EPOLLWRBAND;
  }
  if (events & Err) {
    epollEvents |= EPOLLERR;
  }
  if (events & Hup) {
    epollEvents |= EPOLLHUP;
  }
  if (events & Rdhup) {
    epollEvents |= EPOLLRDHUP;
  }
  if (events & Nval) {
    // No Nval in epoll
    epollEvents |= EPOLLERR;
  }
  if (events & Msg) {
    epollEvents |= EPOLLMSG;
  }
  if (events & Oneshot) {
    epollEvents |= EPOLLONESHOT;
  }
  return epollEvents;
}
uint32 FromEpoll(uint32 events) {
  uint32 ret = 0;
  if (events & EPOLLRDNORM) {
    ret |= Rdnorm;
  }
  if (events & EPOLLRDBAND) {
    ret |= Rdband;
  }
  if (events & EPOLLIN) {
    ret |= In;
  }
  if (events & EPOLLOUT) {
    ret |= Out;
  }
  if (events & EPOLLPRI) {
    ret |= Pri;
  }
  if (events & EPOLLWRNORM) {
    ret |= Wrnorm;
  }
  if (events & EPOLLWRBAND) {
    ret |= Wrband;
  }
  if (events & EPOLLERR) {
    ret |= Err;
  }
  if (events & EPOLLHUP) {
    ret |= Hup;
  }
  if (events & EPOLLRDHUP) {
    ret |= Rdhup;
  }
  if (events & EPOLLMSG) {
    ret |= Msg;
  }
  if (events & EPOLLONESHOT) {
    ret |= Oneshot;
  }
  return ret;
}
uint32 ToPoll(uint32 events) {
  uint32 ret = 0;
  if (events & Rdnorm) {
    ret |= POLLRDNORM;
  }
  if (events & Rdband) {
    ret |= POLLRDBAND;
  }
  if (events & In) {
    ret |= POLLIN;
  }
  if (events & Out) {
    ret |= POLLOUT;
  }
  if (events & Pri) {
    ret |= POLLPRI;
  }
  if (events & Wrnorm) {
    ret |= POLLWRNORM;
  }
  if (events & Wrband) {
    ret |= POLLWRBAND;
  }
  if (events & Err) {
    ret |= POLLERR;
  }
  if (events & Hup) {
    ret |= POLLHUP;
  }
  if (events & Rdhup) {
    ret |= POLLRDHUP;
  }
  if (events & Nval) {
    ret |= POLLNVAL;
  }

  return ret;
}
uint32 FromPoll(uint32 events) {
  uint32 ret = 0;
  if (events & POLLIN) {
    ret |= In;
  }
  if (events & POLLOUT) {
    ret |= Out;
  }
  if (events & POLLRDNORM) {
    ret |= Rdnorm;
  }
  if (events & POLLRDBAND) {
    ret |= Rdband;
  }
  if (events & POLLWRNORM) {
    ret |= Wrnorm;
  }
  if (events & POLLWRBAND) {
    ret |= Wrband;
  }
  if (events & POLLPRI) {
    ret |= Pri;
  }
  if (events & POLLERR) {
    ret |= Err;
  }
  if (events & POLLHUP) {
    ret |= Hup;
  }
  if (events & POLLRDHUP) {
    ret |= Rdhup;
  }
  if (events & POLLNVAL) {
    ret |= Nval;
  }

  return ret;
}

const char *REventToString(EventEnum events) {
  switch (events) {
  case Rdnorm:
    return "Rdnorm";
  case Rdband:
    return "Rdband";
  case Pri:
    return "Pri";
  case Wrnorm:
    return "Wrnorm";
  case Wrband:
    return "Wrband";
  case Err:
    return "Err";
  case Hup:
    return "Hup";
  case Rdhup:
    return "Rdhup";
  case Nval:
    return "Nval";
  case In:
    return "In";
  case Out:
    return "Out";
  case Msg:
    return "Msg";
  case Oneshot:
    return "Oneshot";
    break;
  }
  return "Unknown!";
}

} // namespace REvent

} // namespace Fei