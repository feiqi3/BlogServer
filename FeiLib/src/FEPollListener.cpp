#include "FEPollListener.h"
#include "FDef.h"
#include "FEvent.h"
#include "FREventDef.h"
#include "FSocket.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#define MAX_EVENTS 1024

namespace Fei {

void FEPollListener::listen(uint32 timeoutMs,
                            std::vector<FEventPtr> &outEvents) {
  FEpollEvent EPollevents[MAX_EVENTS];
  int num = EPollWait(m_epollfd, EPollevents, m_pollEvents.size(), timeoutMs);
  if (num >= 0) {
    for (int i = 0; i < num; i++) {
      // Store id in EPollevevnts.data.u64
      uint64 id = EPollevents[i].data.u64;
      auto itor = m_pollEvents.find(id);
      if (itor != m_pollEvents.end()) {
        std::shared_ptr<FEvent> event = itor->second.event.lock();
        if(event == nullptr){
          //Should not happen!!!!!
          m_pollEvents.erase(itor);
          continue;
        }
        setRevents(event.get(), REvent::FromEpoll(EPollevents[i].events));
        outEvents.push_back(event);
        assert(event->getId() == id);
      } else {
        assert(0);
      }
    }
  } else {
    // Error
  }
}

void FEPollListener::addEvent(FEvent* event) {
  //fix me
  EpollData data{.event = event->shared_from_this(), .epollevent = {}};

  // Important Use FEvent id as user data
  data.epollevent.data.u64 = event->getId();

  uint32 eventsAttracted = event->getEvents();
  eventsAttracted = REvent::ToEpoll(eventsAttracted);
  data.epollevent.events = eventsAttracted;

  auto itor = m_pollEvents.insert({event->getId(), data});

  auto data_ptr = &itor.first->second.epollevent;
  if (-1 == EPollCtl(this->m_epollfd, EPollOp::Add, event->getFd(), data_ptr)) {
    std::cout << GetErrorStr();
  }
}

void FEPollListener::removeEvent(FEvent* event) {
  auto itor = m_pollEvents.find(event->getId());
  if (itor != m_pollEvents.end()) {
    m_pollEvents.erase(event->getId());
  }
  if (-1 ==
      EPollCtl(m_epollfd, EPollOp::Del, event->getFd(), nullptr)) {
    std::cout << GetErrorStr();
  }
}

void FEPollListener::updateEvent(FEvent* event) {
  auto itor = m_pollEvents.find(event->getId());
  if (itor == m_pollEvents.end()) {
    return;
    // error
  }
  auto &epollEvent = itor->second.epollevent;
  epollEvent.events = REvent::ToEpoll(event->getEvents());
  if (-1 == EPollCtl(m_epollfd, EPollOp::Mod, event->getFd(), &epollEvent)) {
    std::cout << GetErrorStr();
  }
}

FEPollListener::FEPollListener() {
  m_epollfd = EPollCreate1(0);
  if (-1ull == (uint64)m_epollfd) {
    std::cout << GetErrorStr();
  }
}

FEPollListener::~FEPollListener() {
    EPollClose(m_epollfd);
}

} // namespace Fei