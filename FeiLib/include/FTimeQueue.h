#ifndef FTimeQueue_h
#define FTimeQueue_h

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#include "FDef.h"

namespace Fei {
using TimerFunc = std::function<void()>;
using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
using TimeStamp = uint64;
using TimerID = uint64;

struct FTimerTask {
  TimerID id;
  TimerFunc task;
  bool aborted = false;
};

class F_API FTimeQueue {
public:
  FTimeQueue() = default;
  ~FTimeQueue() = default;
  void AddTask(const std::function<void()>& task);
  uint64 AddTask(const std::function<void()>& task, TimePoint time);
  std::vector<TimerFunc> Tick(TimePoint t);
  void CancelTask(TimerID id); 
private:
std::mutex m_mutex;
  uint64 m_timerCounter;
  std::multimap<TimePoint,FTimerTask> m_tasks;
  std::unordered_map<TimerID, TimePoint> m_timerID2Time;
  std::vector<TimerFunc> m_curTasks;
};
} // namespace Fei

#endif