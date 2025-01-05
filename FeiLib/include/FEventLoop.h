#ifndef FEVENTLOOP_H
#define FEVENTLOOP_H
#include "FAcceptor.h"
#include "FDef.h"
#include "FListener.h"
#include "FNoCopyable.h"
#include "FTimeQueue.h"
#include <functional>
#include <memory>
#include <vector>

namespace Fei {

class FPoller;
class FTimeQueue;
class FListener;
class FEvent;

class F_API FEventLoop : public FNoCopyable {
public:
  FEventLoop(std::unique_ptr<FListener> listener);
  ~FEventLoop();
  void Loop();
  void Quit();
  void AddTask(const std::function<void()> &task);
  TimerID RunAfter(uint64 ms, TimerFunc task);
  void CancelTimer(TimerID id);
  void AddEvent(FEvent* event);
  void RemoveEvent(FEvent* event);
  void UpdateEvent(FEvent* event);
  uint64 getUniqueIdInLoop(){return mIdCounter++; }
private:
  std::atomic_bool m_quit;
  std::unique_ptr<FTimeQueue> m_timeQueue;
  std::unique_ptr<FListener> m_listener;
  std::vector<FEvent*> mActiveEvents;
  uint64 mIdCounter = 1;
};
} // namespace Fei

#endif