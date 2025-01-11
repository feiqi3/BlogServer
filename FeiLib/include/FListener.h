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
                      std::vector<FEventPtr> &outEvents) = 0;
  virtual void addEvent(FEvent* event) = 0;
  virtual void removeEvent(FEvent* event) = 0;
  virtual void updateEvent(FEvent* event) = 0;

public:
  void setRevents(FEvent* event, Event revents);
  uint64 getId(){return m_idCounter++;}
private:
  uint64 m_idCounter = 1;
};
} // namespace Fei

#endif