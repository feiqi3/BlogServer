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

// Some MACRO in system header may consist of multi flag, which cant be match with "&"
bool binMatch(uint32 a, uint32 b_) {
    return (a & b_) == b_;
}

uint32 FromEpoll(uint32 events) {
  uint32 ret = 0;
  if (binMatch(events , EPOLLRDNORM)) {
    ret |= Rdnorm;
  }
  if (binMatch(events , EPOLLRDBAND)) {
    ret |= Rdband;
  }
  if (binMatch(events , EPOLLIN)) {
    ret |= In;
  }
  if (binMatch(events , EPOLLOUT)) {
    ret |= Out;
  }
  if (binMatch(events , EPOLLPRI)) {
    ret |= Pri;
  }
  if (binMatch(events , EPOLLWRNORM)) {
    ret |= Wrnorm;
  }
  if (binMatch(events , EPOLLWRBAND)) {
    ret |= Wrband;
  }
  if (binMatch(events , EPOLLERR)) {
    ret |= Err;
  }
  if (binMatch(events , EPOLLHUP)) {
    ret |= Hup;
  }
  if (binMatch(events , EPOLLRDHUP)) {
    ret |= Rdhup;
  }
  if (binMatch(events , EPOLLMSG)) {
    ret |= Msg;
  }
  if (binMatch(events , EPOLLONESHOT)) {
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

//TODO: use binMatch()
uint32 FromPoll(uint32 events) {
  uint32 ret = 0;
  if (binMatch(events ,POLLIN)) {
    ret |= In;
  }
  if (binMatch(events , POLLOUT)) {
    ret |= Out;
  }
  if (binMatch(events , POLLRDNORM)) {
    ret |= Rdnorm;
  }
  if (binMatch(events , POLLRDBAND)) {
    ret |= Rdband;
  }
  if (binMatch(events , POLLWRNORM)) {
    ret |= Wrnorm;
  }
  if (binMatch(events , POLLWRBAND)) {
    ret |= Wrband;
  }
  if (binMatch(events , POLLPRI)) {
    ret |= Pri;
  }
  if (binMatch(events , POLLERR)) {
    ret |= Err;
  }
  if (binMatch(events , POLLHUP)) {
    ret |= Hup;
  }
  if (binMatch(events , POLLRDHUP)) {
    ret |= Rdhup;
  }
  if (binMatch(events , POLLNVAL)) {
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