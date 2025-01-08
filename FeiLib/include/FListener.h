#ifndef FLISTENER_H
#define FLISTENER_H
#include "FDef.h"
#include <memory>
#include <vector>
namespace Fei {

class F_API FListener {
public:
  virtual ~FListener(){};

  virtual void listen(uint32 timeoutMs,
                      std::vector<std::shared_ptr<class FEvent >> &outEvents) = 0;
  virtual void addEvent(const FEventPtr& event) = 0;
  virtual void removeEvent(const FEventPtr& event) = 0;
  virtual void updateEvent(const FEventPtr& event) = 0;

public:
  void setRevents(const FEventPtr& event, Event revents);
  uint64 getId(){return m_idCounter++;}
private:
  uint64 m_idCounter = 1;
};
} // namespace Fei

#endif