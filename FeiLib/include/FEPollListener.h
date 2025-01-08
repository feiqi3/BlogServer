#ifndef FEPOLLLISTENER_H
#define FEPOLLLISTENER_H
#include "FDef.h"
#include "FListener.h"
#include <map>
#include <memory>
#include <vector>
namespace Fei {
class F_API FEPollListener : public FListener {
public:
  virtual void listen(uint32 timeoutMs,
                      std::vector<std::shared_ptr<FEvent >> &outEvents) override;

  virtual void addEvent(const FEventPtr& event) override;
  virtual void removeEvent(const FEventPtr& event) override;
  virtual void updateEvent(const FEventPtr& event) override;
  ~FEPollListener();
  FEPollListener();

private:
  struct EpollData {
    FEventPtrWeak event;
    FEpollEvent epollevent;
  };
  EpollHandle m_epollfd;
  std::map<uint64, EpollData> m_pollEvents;
};
} // namespace Fei

#endif