#include "FEvent.h"
#include "FDef.h"
#include "FEventLoop.h"
#include "FREventDef.h"
#include "FSocket.h"
#include <iostream>
namespace Fei {

static const Event NoneEvent = 0;
static const Event ReadEvent =
    REvent::In | REvent::Pri | REvent::Rdnorm | REvent::Rdband;
static const Event WriteEvent = REvent::Out;

FEventPtr FEvent::createEvent(FEventLoop *loop, Socket fd, uint64 id) {
  auto ret = std::make_shared<FEvent>(loop, fd, id);
  ret->addSelfToLoop();
  return ret;
}

FEvent::FEvent(FEventLoop *loop, Socket fd, uint64 id)
    : mLoop(loop), mFd(fd), mId(id), mEvent(0), mRevents(0),
      mEventHandling(false), mAddedToLoop(false) {}
FEvent::~FEvent() {
  if (!mAddedToLoop)
    return;
  disableAll();
  mLoop->RemoveEvent(this);
}

void FEvent::addSelfToLoop() {
  if (mAddedToLoop)
    return;
  mLoop->AddEvent(this);
  mAddedToLoop = true;
}

void FEvent::enableReading() {
  if (mEvent & ReadEvent)
    return;
  mEvent |= ReadEvent;
  update();
}

void FEvent::enableWriting() {
  if (mEvent & WriteEvent)
    return;
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

void FEvent::update() {
  if (!mAddedToLoop)
    return;
  mLoop->UpdateEvent(this);
}

void FEvent::remove() {
  if (!mAddedToLoop)
    return;
  mLoop->RemoveEvent(this);
  mAddedToLoop = false;
}

void FEvent::handleEvent() {
  //Removed from loop?
  if (!mAddedToLoop)
    return;
  
  mEventHandling = true;
  if (mRevents & REvent::Nval) {
  }

//------------------------------------------//
// prevent from reacting after conn closed.

  if (mAddedToLoop && (mRevents & REvent::Hup) && !(mRevents & REvent::In)) {
    if (mCloseCallback)
      mCloseCallback();
  }

  if (mAddedToLoop && mRevents & (REvent::Err | REvent::Nval)) {
    if (mErrorCallback)
      mErrorCallback();
  }
  if (mAddedToLoop && mRevents & (REvent::In | REvent::Pri | REvent::Rdhup)) {
    if (mReadCallback)
      mReadCallback();
  }
  if (mAddedToLoop && mRevents & REvent::Out) {
    if (mWriteCallback)
      mWriteCallback();
  }
  mEventHandling = false;
}

} // namespace Fei