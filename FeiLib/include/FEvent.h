#ifndef FEVENT_H
#define FEVENT_H
#include "FCallBackDef.h"
#include "FDef.h"
#include "FListener.h"
#include "FTimeQueue.h"
#include <atomic>
#include <memory>

namespace Fei {

class FListener;
class FEventLoop;

class F_API FEvent : public std::enable_shared_from_this<FEvent> {
public:
  friend class FListener;
  static FEventPtr createEvent(FEventLoop *loop, Socket fd, uint64 id);

public:
  FEvent(FEventLoop *loop, Socket fd, uint64 id);
  ~FEvent();

public:
  void setReveiveTime(TimeStamp reveiveTime) { mReceiveTime = reveiveTime; }
  TimeStamp getReceiveTime()const { return mReceiveTime; }
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
  void addSelfToLoop();

private:
  FEventLoop *mLoop;

private:
  Socket mFd;
  uint64 mId;
  AtomicEvent mEvent;   // Set by user
  AtomicEvent mRevents; // Set by poller
  std::atomic_bool mEventHandling;
  bool mAddedToLoop;
  ReadEventCallback mReadCallback;
  EventCallback mWriteCallback;
  EventCallback mCloseCallback;
  EventCallback mErrorCallback;

  TimeStamp mReceiveTime;
};

} // namespace Fei

#endif