#include "FEventLoop.h"
#include "FListener.h"
#include "FTimeQueue.h"
#include "FEvent.h"
#include <chrono>

namespace Fei {
FEventLoop::FEventLoop(std::unique_ptr<class FListener> listener)
    : m_quit(false), m_timeQueue(new FTimeQueue),
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

  while (!m_quit) {
    mActiveEvents.clear();
    m_listener->listen(100, mActiveEvents);
    for (auto event : mActiveEvents) {
      event->handleEvent();
    }
    auto activeFuncs = m_timeQueue->Tick(std::chrono::system_clock::now());
    for (auto func : activeFuncs) {
      func();
    }
  }
}

void FEventLoop::CancelTimer(TimerID id) { m_timeQueue->CancelTask(id); }

void FEventLoop::AddEvent(FEvent *event) { m_listener->addEvent(event); }
void FEventLoop::RemoveEvent(FEvent *event) { m_listener->removeEvent(event); }
void FEventLoop::UpdateEvent(FEvent *event) { m_listener->updateEvent(event); }

} // namespace Fei