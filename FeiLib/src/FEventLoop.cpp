#include "FEventLoop.h"
#include "FDef.h"
#include "FEvent.h"
#include "FListener.h"
#include "FTimeQueue.h"
#include <chrono>
#include <memory>
#include <thread>

namespace Fei {

static thread_local uint64 _this_thread_loop = 0;

FEventLoop::FEventLoop(std::unique_ptr<class FListener> listener)
    : m_stoped(false), m_quit(false), m_timeQueue(new FTimeQueue),
      m_listener(std::move(listener)) {}

void FEventLoop::Quit() { m_quit = true; }

void FEventLoop::AddTask(const std::function<void()> &task) {
  m_timeQueue->AddTask(task);
}
TimerID FEventLoop::RunAfter(uint64 ms, TimerFunc task) {
  return m_timeQueue->AddTask(task, std::chrono::system_clock::now() +
                                        std::chrono::milliseconds(ms));
}

void FEventLoop::Loop() {
  if (_this_thread_loop != 0) {
    m_quit = true;
  } else {
    _this_thread_loop = (uint64)this;
  }
  while (!m_quit) {
    mActiveEvents.clear();
    m_listener->listen(10, mActiveEvents);
    for (auto& event : mActiveEvents) {
      if (m_forceQuit)
        break;
      event->setEventHandling(true);
      event->handleEvent();
      event->setEventHandling(false);
    }
    auto activeFuncs = m_timeQueue->Tick(std::chrono::system_clock::now());
    for (auto func : activeFuncs) {
      if (m_forceQuit)
        break;
      func();
    }
  }
  m_stoped = true;
}

void FEventLoop::CancelTimer(TimerID id) { m_timeQueue->CancelTask(id); }

void FEventLoop::AddEvent(FEvent* event) {
  m_listener->addEvent(event);
}
void FEventLoop::RemoveEvent(FEvent* event) { m_listener->removeEvent(event); }
void FEventLoop::UpdateEvent(FEvent* event) { m_listener->updateEvent(event); }
bool FEventLoop::isInLoopThread() const {
  return _this_thread_loop == (uint64)this;
}
} // namespace Fei