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

  virtual void addEvent(std::shared_ptr<FEvent> event) override;
  virtual void removeEvent(class FEvent *event) override;
  virtual void updateEvent(class FEvent *event) override;
  ~FEPollListener();
  FEPollListener();

private:
  struct EpollData {
    std::shared_ptr<FEvent> event;
    FEpollEvent epollevent;
  };
  EpollHandle m_epollfd;
  std::map<uint64, EpollData> m_pollEvents;
};
} // namespace Fei

#endif