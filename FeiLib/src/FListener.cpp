#include "FListener.h"
#include "FEvent.h"
namespace Fei {
void FListener::setRevents(const FEventPtr& event, Event revents) {
  event->setRevents(revents);
}




} // namespace Fei