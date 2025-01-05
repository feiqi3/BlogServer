#include "FDef.h"
#include <cassert>
#include <vector>
#include "FTimeQueue.h"

namespace Fei {

void FTimeQueue::AddTask(const std::function<void()>& task) {
    FAUTO_LOCK(m_mutex);
    m_curTasks.push_back(task);
}

uint64 FTimeQueue::AddTask(const std::function<void()>& task, TimePoint time) {
    FAUTO_LOCK(m_mutex);
    FTimerTask timerTask{};
    timerTask.task = task;

    TimerID id = m_timerCounter++;
    timerTask.id =id;
    m_tasks.insert({time, timerTask});
    m_timerID2Time.insert(std::make_pair(id, time));
    return id;
}

std::vector<TimerFunc> FTimeQueue::Tick(TimePoint t) {
    FAUTO_LOCK(m_mutex);
    auto timerItBegin = m_tasks.begin();
    auto timerItEnd = m_tasks.upper_bound(t);
    std::vector<TimerFunc> ret;
    std::swap(ret, m_curTasks);

    for (auto it = timerItBegin; it != timerItEnd;) {
        if (!it->second.aborted) {
            ret.push_back(it->second.task);
        }
        m_timerID2Time.erase(it->second.id);
        it = m_tasks.erase(it);
    }
    return ret;
}

  void FTimeQueue::CancelTask(TimerID id){
    FAUTO_LOCK(m_mutex);
    auto timeIt = m_timerID2Time.find(id);
    if(timeIt == m_timerID2Time.end())return;
    auto taskIt = m_tasks.equal_range(timeIt->second);
    assert(taskIt.first != taskIt.second);
    for(auto it = taskIt.first; it != taskIt.second; ++it){
        if(it->second.id == id){
            it->second.aborted = true;
            m_timerID2Time.erase(id);
            return;
        }
    }
  }

}