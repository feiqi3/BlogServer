#ifndef FTCPCONNECTION_H
#define FTCPCONNECTION_H

#include <cassert>
#include <memory>
#include <string>

#include "FCallBackDef.h"
#include "FDef.h"
#include "FNoCopyable.h"

namespace Fei {
class FEventLoop;
class FEvent;
class FSock;
class FBuffer;

class F_API FTcpConnection
    : public FNoCopyable,
      public std::enable_shared_from_this<FTcpConnection> {
public:
  friend class FTcpServer;

  enum class TcpConnState {
    Connected,
    DisConnected,
    Connectiing,
    DisConnecting
  };

  static FTcpConnPtr makeConn(FEventLoop *loop, Socket s, FSocketAddr addrIn, FSocketAddr addrAccept) {
    return std::make_shared<FTcpConnection>(loop, s, addrIn,addrAccept);
  }

  // High water callback: when there are too much data in inBuffer/outBuffer
  //                      While user consume it slow, then this will be called
  // Low  water callback: when data in buffer reach a user set limit, call this.

  FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn,FSocketAddr addrAccept);
  ~FTcpConnection();

  TcpConnState getState() const { return mstate; }
  void setReading(bool v);
  void send(const char *data, uint64 len);
  void send(std::string&& data);
  void setKeepAlive(bool v);
  void setKeepIdle(int idleTime);
  void setKeepInterval(int intervalTime);

  void forceClose();
  void forceCloseInDelay(uint32 ms);

  FSocketAddr getAddr() const { return m_addrIn; }
  FSocketAddr getAddrAccept()const{return m_addrAccept;}
protected:
  // When output buffer is empty, send directly,
  // else queued in loop and send by buffer.
  void sendInLoop(const char *data, uint64 len);
  void handleRead();
  void handleWrite();
  void handleClose();
  void handleError(Errno_t err);
  void shutdownInLoop();
  void handleWriteComplete() {}
  void startReadingInLoop();
  void stopReadingInLoop();

  void sendInLoopStr(std::string data);
  void forceCloseInLoop();

  Socket getFd();

  void setMessageCallback(TcpMessageCallback cb) {
    m_onMessage = std::move(cb);
  }
  void setWriteCompleteCallback(TcpWriteCompleteCallback cb) {
    m_onWriteComplete = std::move(cb);
  }
  void setCloseCallback(TcpCloseCallback cb) {
    m_onCloseCallback = std::move(cb);
  }

  FEventLoop *m_loop;
  std::unique_ptr<FSock> m_sock;

  FSocketAddr m_addrIn;
  FSocketAddr m_addrAccept;
  std::shared_ptr<FEvent> m_event;

  std::unique_ptr<FBuffer> inBuffer;
  std::unique_ptr<FBuffer> outBuffer;
  TcpMessageCallback m_onMessage;
  TcpWriteCompleteCallback m_onWriteComplete;
  TcpCloseCallback m_onCloseCallback;

  TcpConnState mstate;
};

} // namespace Fei

#endif