#include "FBufferReader.h"
#include "FCallBackDef.h"
#include "FDef.h"

#include "FBuffer.h"
#include "FEvent.h"
#include "FEventLoop.h"
#include "FException.h"
#include "FLogger.h"
#include "FSSLHelper.h"
#include "FSockWrapper.h"
#include "FSocket.h"
#include "FTCPConnection.h"
#include "FWeakCallback.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#define MODULE_NAME "TcpConn"

namespace Fei {

FTcpConnection::~FTcpConnection() {
  bool isInLoopThread = m_loop->isInLoopThread();
  if (isInLoopThread) {
    cancelIdleFunc();
    assert(mstate == TcpConnState::DisConnected);
    // Remove event out of loop
    m_event->remove();
  }else{
    //Only happened when Server is closed
    m_event->setAddedToLoop(false);
    //Event remove is not thread safe
  }
}

FTcpConnection::FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn,
                               FSocketAddr addrAccept, bool sslSupport)
    : m_loop(loop), m_sock(new FSock(s)), m_addrIn(addrIn),
      m_addrAccept(addrAccept),
      m_event(FEvent::createEvent(loop, s, loop->getUniqueIdInLoop())),
      inBuffer(std::make_unique<FBuffer>(1024)),
      outBuffer(std::make_unique<FBuffer>(1024)),
      mstate(TcpConnState::Connectiing) {
  m_event->setCloseCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setErrorCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setReadCallback(std::bind(&FTcpConnection::handleRead, this));
  m_event->setWriteCallback(std::bind(&FTcpConnection::handleWrite, this));
  m_event->setPostEventCallback(std::bind(&FTcpConnection::handlePostEvent, this));
  Logger::instance()->log(
      MODULE_NAME, lvl::trace,
      "TcpConnection Establish. address: {}.{}.{}.{}, port: {}",
      m_addrIn.un.un_byte.a0, m_addrIn.un.un_byte.a1, m_addrIn.un.un_byte.a2,
      m_addrIn.un.un_byte.a3, m_addrIn.port);
  if (sslSupport) {
    sslHelper = std::make_unique<FSSLHelper>();
  }
}

void FTcpConnection::sendInLoop(char *data, int len) {
  m_loop->isInLoopAssert();
  if (mstate == TcpConnState::DisConnected) {
    return;
  }

  SocketStatus status = SocketStatus::Success;
  const char* sendData = data;
  int sendLen = 0;
  try {
    if (isSSLConnection() && sslHelper->hasShakeHandFin()) {
      auto reader = sslHelper->EncryptSendingData(data, len);
      sendData = (const char *)reader.peekAll(len);
      reader.expireSize(len);
      //!!!!! Important : in current implementation we dont free the ptr to data
      //! immediately, so this is legal !!!!!
    }
  } catch (FException &e) {
    Logger::instance()->log(MODULE_NAME, lvl::warn, "%s", e.what());
    handleClose();
    return;
  }

  // A simple clone of muduo
  // Send directly if no data in buffer
  int remaining = len;
  bool faultError = false;

  if (!m_event->isWriting() && outBuffer->getReadableSize() == 0) {
    status = Send(m_sock->getFd(), sendData, len, sendLen);
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
    outBuffer->Append(sendData + sendLen, remaining);
    if (!m_event->isWriting()) {
      m_event->enableWriting();
    }
  }
}

void FTcpConnection::handleRead() {
  m_loop->isInLoopAssert();
  if (mstate == TcpConnState::DisConnected)
    return;
  Errno_t err = 0;
  auto len = this->inBuffer->Read(m_sock->getFd(), err);

  mstate = TcpConnState::Connected;
  if (len > 0) {
    FBufferReader reader(*inBuffer);

    if (isSSLConnection()) {
      //For tcp connection
      try {
        if (sslHelper->shakeHand(this, reader)) {
          auto newReader = sslHelper->DecryptRecvingData(reader);
          if (m_onMessage) {
            m_onMessage(shared_from_this(), newReader);
          }
        }
      } catch (FException &e) {
        Logger::instance()->log(MODULE_NAME, lvl::warn,"%s", e.what());
        handleClose();
        return;
      }

    } else {
      if (m_onMessage) {
        m_onMessage(shared_from_this(), reader);
      }
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

void FTcpConnection::setKeepInterval(int intervalTime) {
  m_sock->setKeepInterval(intervalTime);
}

void FTcpConnection::forceClose() {
  if (m_loop->isInLoopThread()) {
    forceCloseInLoop();
  } else {
    auto func =
        makeWeakFunction(weak_from_this(), &FTcpConnection::forceCloseInLoop);
    m_loop->AddTask(func);
  }
}

void FTcpConnection::forceCloseInDelay(uint32 ms) {
  auto func =
      makeWeakFunction(weak_from_this(), &FTcpConnection::forceCloseInLoop);
  m_loop->RunAfter(ms, func);
}
FTcpConnPtr FTcpConnection::makeConn(FEventLoop *loop, Socket s, FSocketAddr addrIn, FSocketAddr addrAccept,bool sslSupport) {
    auto ret = std::make_shared<FTcpConnection>(loop, s, addrIn,addrAccept,sslSupport);
    ret->m_event->init();
    return ret;
  }

void FTcpConnection::setReading(bool v) {

  if (v == true && !m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      startReadingInLoop();
      return;
    }
    m_loop->AddTask(
        std::bind(&FTcpConnection::startReadingInLoop, shared_from_this()));
  } else if (v == false && m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      stopReadingInLoop();
      return;
    }
    m_loop->AddTask(
        std::bind(&FTcpConnection::stopReadingInLoop, shared_from_this()));
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
  // No further code should be here
}

void FTcpConnection::handleError(Errno_t err) {
  m_loop->isInLoopAssert();
  Logger::instance()->log(MODULE_NAME, lvl::trace,
                          "TcpConnection Error, Errno {}. address: "
                          "{}.{}.{}.{}, port: {}, errInfo: {}",
                          strerror(errno), m_addrIn.un.un_byte.a0,
                          m_addrIn.un.un_byte.a1, m_addrIn.un.un_byte.a2,
                          m_addrIn.un.un_byte.a3, m_addrIn.port, GetErrorStr());
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

void FTcpConnection::send(char *data, uint64 len) {
  if (m_loop->isInLoopThread()) {
    sendInLoop(data, len);
  } else {
    m_loop->AddTask(std::bind(&FTcpConnection::sendInLoopStr,
                              shared_from_this(), std::string(data)));
  }
}

void FTcpConnection::send(std::string &&data) {
  if (m_loop->isInLoopThread()) {
    send(data.data(), data.size());
  } else {
    m_loop->AddTask(std::bind(&FTcpConnection::sendInLoopStr,
                              shared_from_this(), std::move(data)));
  }
}

void FTcpConnection::startReadingInLoop() { m_event->enableReading(); }

void FTcpConnection::stopReadingInLoop() { m_event->disableReading(); }
void FTcpConnection::handleOnIdle(){
  if(!m_onIdleCallback)
  {
    return;
  }
  // Reset the timer
  mIdleTImer = 0;
  resetIdle();
  m_onIdleCallback(shared_from_this());

}

void FTcpConnection::sendInLoopStr(std::string data) {
  sendInLoop(data.data(), (int)data.size());
}

void FTcpConnection::resetIdle(){
  cancelIdleFunc();
  if (m_onIdleCallback && mIdleTime > 0){
    // millisecond
    if (mstate != TcpConnState::DisConnected) {
      auto func = makeWeakFunction(weak_from_this(), &FTcpConnection::handleOnIdle);
      mIdleTImer = m_loop->RunAfter((uint64)mIdleTime, func);
    }
  }
}

void FTcpConnection::cancelIdleFunc(){
  if(mIdleTImer > 0){
    m_loop->CancelTimer(mIdleTImer);
  }
}

void FTcpConnection::forceCloseInLoop() {
  m_loop->isInLoopAssert();
  handleClose();
}

} // namespace Fei