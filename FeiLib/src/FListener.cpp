#include "FListener.h"
#include "FEvent.h"
namespace Fei {
void FListener::setRevents(FEvent* event, Event revents) {
  event->setRevents(revents);
}




} // namespace Fei