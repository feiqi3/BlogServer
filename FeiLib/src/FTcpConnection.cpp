#include "FBufferReader.h"
#include "FCallBackDef.h"
#include "FDef.h"

#include "FBuffer.h"
#include "FEvent.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSockWrapper.h"
#include "FSocket.h"
#include "FTCPConnection.h"
#include "FWeakCallback.h"

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#define MODULE_NAME "TcpConn"

namespace Fei {

void guardian(FTcpConnPtr ptr) {
  (void)ptr;
  // Do Nothing, but hold a reference count and queued in loop after the need
  // protected task, to protect this conn from being destroyed.
}

FTcpConnection::~FTcpConnection() {
  m_loop->isInLoopAssert();
  assert(mstate == TcpConnState::DisConnected);
  //Remove event out of loop
  m_event->remove();
}

FTcpConnection::FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn,FSocketAddr addrAccept)
    : m_loop(loop), m_sock(new FSock(s)), m_addrIn(addrIn),m_addrAccept(addrAccept),
      m_event(FEvent::createEvent(loop, s, loop->getUniqueIdInLoop())),
      inBuffer(std::make_unique<FBuffer>(1024)),
      outBuffer(std::make_unique<FBuffer>(1024)),
      mstate(TcpConnState::Connectiing) {
  m_event->setCloseCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setErrorCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setReadCallback(std::bind(&FTcpConnection::handleRead, this));
  m_event->setWriteCallback(std::bind(&FTcpConnection::handleWrite, this));
  Logger::instance()->log(
      MODULE_NAME, lvl::trace,
      "TcpConnection Establish. address: {}.{}.{}.{}, port: {}",
      m_addrIn.un.un_byte.a0, m_addrIn.un.un_byte.a1, m_addrIn.un.un_byte.a2,
      m_addrIn.un.un_byte.a3, m_addrIn.port);
}

void FTcpConnection::sendInLoop(const char *data, uint64 len) {
  m_loop->isInLoopAssert();
  if (mstate == TcpConnState::DisConnected) {
    return;
  }
  int sendLen = 0;

  SocketStatus status = SocketStatus::Success;
  // A simple clone of muduo
  // Send directly if no data in buffer
  bool faultError = false;
  int remaining = len;
  if (!m_event->isWriting() && outBuffer->getReadableSize() == 0) {
    status = Send(m_sock->getFd(), data, len, sendLen);
    if (status != SocketStatus::Success) {
      auto err = errno;
      sendLen = 0;
      if (err == EPIPE || err == ECONNRESET) {
        faultError = true;
      }
    } else {
      remaining = len - sendLen;
      if (remaining == 0) {
        if (m_onWriteComplete)
          m_loop->AddTask(std::bind(m_onWriteComplete, shared_from_this()));
      }
    }
  }
  if (!faultError && remaining > 0) {
    outBuffer->Append(data + sendLen, remaining);
    if (!m_event->isWriting()) {
      m_event->enableWriting();
    }
  }
}

void FTcpConnection::handleRead() {
  m_loop->isInLoopAssert();
  if(mstate == TcpConnState::DisConnected)
    return;
  Errno_t err = 0;
  auto len = this->inBuffer->Read(m_sock->getFd(), err);

  mstate = TcpConnState::Connected;
  if (len > 0) {
    if (m_onMessage) {
      FBufferReader reader(*inBuffer);
      m_onMessage(shared_from_this(), reader);
    }
  } else if (len == 0) {
    handleClose();
  } else {
    // Error
    handleError(err);
  }
}

void FTcpConnection::setKeepAlive(bool v) { m_sock->setKeepAlive(v); }

void FTcpConnection::setKeepIdle(int idleTime) {
  m_sock->setKeepIdle(idleTime);
}

void FTcpConnection::setKeepInterval(int intervalTime)
{
    m_sock->setKeepInterval(intervalTime);
}

void FTcpConnection::forceClose()
{
    if (m_loop->isInLoopThread()) {
        forceCloseInLoop();
    }
    else {
        auto func = makeWeakFunction(weak_from_this(), &FTcpConnection::forceCloseInLoop);
        m_loop->AddTask(func);
    }
}

void FTcpConnection::forceCloseInDelay(uint32 ms)
{
    auto func = makeWeakFunction(weak_from_this(), &FTcpConnection::forceCloseInLoop);
    m_loop->RunAfter(ms, func);
}

void FTcpConnection::setReading(bool v) {

  if (v == true && !m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      startReadingInLoop();
      return;
    }
    m_loop->AddTask(std::bind(&FTcpConnection::startReadingInLoop, shared_from_this()));
  } else if (v == false && m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      stopReadingInLoop();
      return;
    }
    m_loop->AddTask(std::bind(&FTcpConnection::stopReadingInLoop, shared_from_this()));
  }
}

void FTcpConnection::handleClose() {
  m_loop->isInLoopAssert();
  mstate = TcpConnState::DisConnected;
  m_event->disableAll();
  m_onWriteComplete = nullptr;
  m_onMessage = nullptr;
  Logger::instance()->log(
      MODULE_NAME, lvl::trace,
      "TcpConnection disconnected. address: {}.{}.{}.{}, port: {}",
      m_addrIn.un.un_byte.a0, m_addrIn.un.un_byte.a1, m_addrIn.un.un_byte.a2,
      m_addrIn.un.un_byte.a3, m_addrIn.port);

  volatile int barrier__ = 0;
  m_onCloseCallback(shared_from_this());
  //No further code should be here
}

void FTcpConnection::handleError(Errno_t err) {
  m_loop->isInLoopAssert();
  Logger::instance()->log(
      MODULE_NAME, lvl::trace,
      "TcpConnection Error, Errno {}. address: {}.{}.{}.{}, port: {}, errInfo: {}",
      strerror(errno), m_addrIn.un.un_byte.a0, m_addrIn.un.un_byte.a1,
      m_addrIn.un.un_byte.a2, m_addrIn.un.un_byte.a3, m_addrIn.port,GetErrorStr());
  bool faultError = false;
    
  if (err != EINTR && err != EWOULDBLOCK && err != EAGAIN) {
      faultError = true;
  }

  if (faultError) {
      this->forceClose();
  }
}

void FTcpConnection::handleWrite() {
  m_loop->isInLoopAssert();
  if (!m_event->isWriting() || mstate == TcpConnState::DisConnected) {
    return;
  }
  Errno_t err = 0;
  auto len =
      outBuffer->Write(m_event->getFd(), outBuffer->getReadableSize(), err);
  if (len > 0) {
    outBuffer->Pop(len);
    if (outBuffer->getReadableSize() == 0) {
      m_event->disableWriting();
      if (m_onWriteComplete) {
        m_loop->AddTask(std::bind(m_onWriteComplete, shared_from_this()));
      }
      if (mstate == TcpConnState::DisConnecting) {
        Logger::instance()->log(
            MODULE_NAME, lvl::trace,
            "TcpConnection shuting down. address: {}.{}.{}.{}, port: {}",
            strerror(errno), m_addrIn.un.un_byte.a0, m_addrIn.un.un_byte.a1,
            m_addrIn.un.un_byte.a2, m_addrIn.un.un_byte.a3, m_addrIn.port);
        shutdownInLoop();
      }
    }
  }
}

void FTcpConnection::shutdownInLoop() {
  m_loop->isInLoopAssert();
  mstate = TcpConnState::DisConnecting;
  if (!m_event->isWriting()) {
    ShutDown(m_sock->getFd(), true, false);
  }
}

Socket FTcpConnection::getFd() { return m_sock->getFd(); }

void FTcpConnection::send(const char *data, uint64 len) {
  if (m_loop->isInLoopThread()) {
    sendInLoop(data, len);
  } else {
    m_loop->AddTask(
        std::bind(&FTcpConnection::sendInLoopStr, shared_from_this(), std::string(data)));
  }
}

void FTcpConnection::send(std::string&& data)
{
    if (m_loop->isInLoopThread()) {
        sendInLoop(data.data(), data.size());
    }
    else {
        m_loop->AddTask(
            std::bind(&FTcpConnection::sendInLoopStr, shared_from_this(), std::move(data)));
    }
}

void FTcpConnection::startReadingInLoop() { m_event->enableReading(); }

void FTcpConnection::stopReadingInLoop() { m_event->disableReading(); }

void FTcpConnection::sendInLoopStr(std::string data) {
  sendInLoop(data.data(), data.size());
}

void FTcpConnection::forceCloseInLoop()
{
    m_loop->isInLoopAssert();
    handleClose();
}

} // namespace Fei