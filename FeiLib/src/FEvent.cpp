#include "FEvent.h"
#include "FDef.h"
#include "FEventLoop.h"
#include "FREventDef.h"
#include "FSocket.h"
namespace Fei {

static const Event NoneEvent = 0;
static const Event ReadEvent = REvent::In | REvent::Pri;
static const Event WriteEvent = REvent::Out;

FEvent::FEvent(FEventLoop *loop, Socket fd, uint64 id)
    : mLoop(loop), mFd(fd), mId(id), mEvent(0), mRevents(0),
      mEventHandling(false), mAddedToLoop(false) {
  mLoop->AddEvent(this);
}
FEvent::~FEvent() {
  disableAll();
  mLoop->RemoveEvent(this);
}

void FEvent::enableReading() {
  mEvent |= ReadEvent;
  update();
}

void FEvent::enableWriting() {
  mEvent |= WriteEvent;
  update();
}

void FEvent::disableReading() {
  mEvent &= ~ReadEvent;
  update();
}

void FEvent::disableWriting() {
  mEvent &= ~WriteEvent;
  update();
}

void FEvent::disableAll() {
  mEvent = NoneEvent;
  update();
}

bool FEvent::isWriting() { return mEvent & WriteEvent; }

bool FEvent::isReading() { return mEvent & ReadEvent; }

bool FEvent::isNoneEvent() { return mEvent == NoneEvent; }

void FEvent::update() { mLoop->UpdateEvent(this); }

void FEvent::remove() { mLoop->RemoveEvent(this); }

void FEvent::handleEvent() {
  mEventHandling = true;
  if (mRevents & REvent::Nval) {
  }
  if ((mRevents & REvent::Hup) && !(mRevents & REvent::In)) {
    if (mCloseCallback)
      mCloseCallback();
  }
  if (mRevents & (REvent::Err | REvent::Nval)) {
    if (mErrorCallback)
      mErrorCallback();
  }
  if (mRevents & (REvent::In | REvent::Pri | REvent::Rdhup)) {
    if (mReadCallback)
      mReadCallback();
  }
  if (mRevents & REvent::Out) {
    if (mWriteCallback)
      mWriteCallback();
  }
  mEventHandling = false;
}

} // namespace Fei