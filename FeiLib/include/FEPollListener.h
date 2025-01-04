#ifndef FEPOLLLISTENER_H
#define FEPOLLLISTENER_H
#include "FDef.h"
#include "FListener.h"
#include <map>
#include <vector>
namespace Fei {
class FEPollListener : public FListener {
public:
  virtual void listen(uint32 timeoutMs,
                      std::vector<class FEvent *> &outEvents) override;

  virtual void addEvent(class FEvent *event) override;
  virtual void removeEvent(class FEvent *event) override;
  virtual void updateEvent(class FEvent *event) override;
  ~FEPollListener();
  FEPollListener();

private:
  struct EpollData {
    class FEvent *event;
    FEpollEvent epollevent;
  };
  virtual void update(int operation, class FEvent *event) override;
  EpollHandle m_epollfd;
  std::map<uint64, EpollData> m_pollEvents;
};
} // namespace Fei

#endif