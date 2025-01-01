#include "FEventLoop.h"
#include "FTimeQueue.h"

namespace Fei {
FEventLoop::FEventLoop() : m_quit(false), m_timeQueue(new FTimeQueue) {}

void FEventLoop::Quit() { m_quit = true; }

void FEventLoop::AddTask(const std::function<void()> &task) {
  m_timeQueue->AddTask(task);
}
TimerID FEventLoop::RunAfter(uint64 ms, TimerFunc task) {
  return m_timeQueue->AddTask(task, std::chrono::system_clock::now() +
                                        std::chrono::milliseconds(ms));
}

void FEventLoop::CancelTimer(TimerID id) { m_timeQueue->CancelTask(id); }
} // namespace Fei