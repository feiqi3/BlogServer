#ifndef FEVENT_H
#define FEVENT_H
#include "FDef.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FListener.h"
#include <functional>

namespace Fei {
typedef std::function<void()> EventCallback;
typedef std::function<void()> ReadEventCallback;

class FListener;

class F_API FEvent {
public:
  friend class FListener;
  
public:
  FEvent(FEventLoop *loop, Socket fd, uint64 id);
  ~FEvent();

public:
  void setReadCallback(ReadEventCallback cb) { mReadCallback = std::move(cb); }
  void setWriteCallback(EventCallback cb) { mWriteCallback = std::move(cb); }
  void setCloseCallback(EventCallback cb) { mCloseCallback = std::move(cb); }
  void setErrorCallback(EventCallback cb) { mErrorCallback = std::move(cb); }
  Event getEvents() const { return mEvent; }
  uint64 getId() const { return mId; }

  void enableReading();
  void enableWriting();
  void disableReading();
  void disableWriting();
  void disableAll();
  bool isWriting();
  bool isReading();
  bool isNoneEvent();
  void update();
  void remove();
  void handleEvent();
  Socket getFd() const { return mFd; }
  Event getRevents() const { return mRevents; }
  void setEventHandling(bool handling) { mEventHandling = handling; }
  bool isEventHandling() const { return mEventHandling; }
  void setAddedToLoop(bool added) { mAddedToLoop = added; }
  bool isAddedToLoop() const { return mAddedToLoop; }

private:
  void setRevents(Event revents) { mRevents = revents; }

private:
  FEventLoop *mLoop;

private:
  Socket mFd;
  uint64 mId;
  Event mEvent;   // Set by user
  Event mRevents; // Set by poller
  bool mEventHandling;
  bool mAddedToLoop;
  ReadEventCallback mReadCallback;
  EventCallback mWriteCallback;
  EventCallback mCloseCallback;
  EventCallback mErrorCallback;
};

} // namespace Fei

#endif