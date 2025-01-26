#ifndef FEVENTLOOP_H
#define FEVENTLOOP_H
#include "FAcceptor.h"
#include "FDef.h"
#include "FListener.h"
#include "FNoCopyable.h"
#include "FTimeQueue.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <vector>

namespace Fei {

class FTimeQueue;
class FListener;
class FEvent;

class F_API FEventLoop : public FNoCopyable {
public:
  FEventLoop(std::unique_ptr<FListener> listener);
  ~FEventLoop(){assert(m_stoped);}
  void Loop();
  void Quit();
  void ForceQuit() {m_forceQuit = true;}
  bool HasStoped() {return m_stoped;}
  void AddTask(const std::function<void()> &task);
  TimerID RunAfter(uint64 ms, TimerFunc task);
  void CancelTimer(TimerID id);
  void AddEvent(FEvent* event);
  void RemoveEvent(FEvent* event);
  void UpdateEvent(FEvent* event);
  uint64 getUniqueIdInLoop(){return mIdCounter++; }

  bool isInLoopThread()const;
  void isInLoopAssert()const{assert(isInLoopThread());}
private:
  std::atomic_bool m_stoped;
  std::atomic_bool m_quit;
  std::unique_ptr<FTimeQueue> m_timeQueue;
  std::unique_ptr<FListener> m_listener;
  std::vector<FEventPtr> mActiveEvents;
  uint64 mIdCounter = 1;
  std::atomic_bool m_forceQuit = false;
};
} // namespace Fei

#endif