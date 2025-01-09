#include "FTCPConnection.h"
#include "FBuffer.h"
#include "FCallBackDef.h"
#include "FDef.h"
#include "FEvent.h"
#include "FSockWrapper.h"
#include "FSocket.h"
#include <cerrno>
#include <functional>
#include <memory>
#include <string>

namespace Fei {

void guardian(FTcpConnPtr ptr) {
  (void)ptr;
  // Do Nothing, but hold a reference count and queued in loop after the need
  // protected task, to protect this conn from being destroyed.
}

FTcpConnection::FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn)
    : m_loop(loop), m_sock(new FSock(s)), m_addrIn(addrIn),
      m_event(std::make_shared<FEvent>(loop, s, loop->getUniqueIdInLoop())),
      inBuffer(std::make_unique<FBuffer>(1024)),
      outBuffer(std::make_unique<FBuffer>(1024)),
      mstate(TcpConnState::Connectiing) {
  m_event->setCloseCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setErrorCallback(std::bind(&FTcpConnection::handleClose, this));
  m_event->setReadCallback(std::bind(&FTcpConnection::handleRead, this));
  m_event->setWriteCallback(std::bind(&FTcpConnection::handleWrite, this));
}

void FTcpConnection::sendInLoop(const char *data, uint64 len) {
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
      int remaining = len - sendLen;
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
  Errno_t err = 0;
  auto len = this->inBuffer->Read(m_sock->getFd(), err);
  mstate = TcpConnState::Connected;
  if (len > 0) {
    if (m_onMessage)
      m_onMessage(shared_from_this(), *inBuffer);
  } else if (len == 0) {
    handleClose();
  } else {
    // Error
    handleError();
  }
}

void FTcpConnection::setReading(bool v) {

  if (v == true && !m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      startReadingInLoop();
      return;
    }
    m_loop->AddTask(std::bind(&FTcpConnection::startReadingInLoop, this));
    m_loop->AddTask(std::bind(&guardian, shared_from_this()));
  } else if (v == false && m_event->isReading()) {
    if (m_loop->isInLoopThread()) {
      stopReadingInLoop();
      return;
    }
    m_loop->AddTask(std::bind(&FTcpConnection::stopReadingInLoop, this));
    m_loop->AddTask(std::bind(&guardian, shared_from_this()));
  }
}

void FTcpConnection::handleClose() {
  mstate = TcpConnState::DisConnected;
  m_event->disableAll();
  m_onCloseCallback(shared_from_this());
}

void FTcpConnection::handleWrite() {
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
        shutdownInLoop();
      }
    }
  }
}

void FTcpConnection::shutdownInLoop() {
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
        std::bind(&FTcpConnection::sendInLoopStr, this, std::string()));
    m_loop->AddTask(std::bind(&guardian, shared_from_this()));
  }
}

void FTcpConnection::destroy() {
  // Must in loop
  m_loop->isInLoopAssert();

  if (mstate != TcpConnState::DisConnected) {
    m_event->disableAll();
  }
}

void FTcpConnection::startReadingInLoop() { m_event->enableReading(); }

void FTcpConnection::stopReadingInLoop() { m_event->disableReading(); }

void FTcpConnection::sendInLoopStr(std::string data) {
  sendInLoop(data.data(), data.size());
}

} // namespace Fei