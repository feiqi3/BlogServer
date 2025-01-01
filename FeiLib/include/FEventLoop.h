#ifndef FEVENTLOOP_H
#define FEVENTLOOP_H
#include "FDef.h"
#include "FNoCopyable.h"
#include <chrono>
#include <functional>
#include "FTimeQueue.h"

namespace Fei {

class FPoller;
class FTimeQueue;

class FEventLoop : public FNoCopyable {
public:
  FEventLoop();
  ~FEventLoop();
  void Loop();
  void Quit();
  void AddTask(const std::function<void()>& task);
  TimerID RunAfter(uint64 ms, TimerFunc task);
  void CancelTimer(TimerID id);

private:
  bool m_quit;
  FTimeQueue *m_timeQueue;
};
} // namespace Fei

#endif