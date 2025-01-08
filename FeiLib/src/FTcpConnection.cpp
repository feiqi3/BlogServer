#include "FTCPConnection.h"
#include "FBuffer.h"
#include "FDef.h"
#include "FEvent.h"
#include "FSockWrapper.h"
#include "FSocket.h"
#include <cerrno>
#include <functional>
#include <memory>

namespace Fei {

FTcpConnection::FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn)
    : m_loop(loop), m_sock(new FSock(s)), m_addrIn(addrIn),
      m_event(std::make_shared<FEvent>(loop, s, loop->getUniqueIdInLoop())),
      inBuffer(std::make_unique<FBuffer>(1024)),
      outBuffer(std::make_unique<FBuffer>(1024)),
      mstate(TcpConnState::Connectiing) {}

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

void FTcpConnection::handleClose() {
  mstate = TcpConnState::DisConnected;
  m_event->disableAll();
  m_onCloseCallback(shared_from_this());
}

void FTcpConnection::handleWrite() {
  if(!m_event->isWriting()){
    return;
  }
  Errno_t err = 0;
  auto len = outBuffer->Write(m_event->getFd(), outBuffer->getReadableSize(),err);
  if(len > 0){
    outBuffer->PopAll();
    if(m_onWriteComplete){
      m_loop->AddTask(std::bind(m_onWriteComplete,shared_from_this()));
    }
  }


}

void FTcpConnection::destroy() {
  // Must in loop
  m_loop->isInLoopAssert();

  if (mstate != TcpConnState::DisConnected) {
    m_event->disableAll();
  }
}

} // namespace Fei