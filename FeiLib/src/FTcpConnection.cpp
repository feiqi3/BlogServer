#include "FTCPConnection.h"
#include "FBuffer.h"
#include "FEvent.h"
#include "FSockWrapper.h"
#include "FSocket.h"
#include <cerrno>
#include <memory>

namespace Fei {

FTcpConnection::FTcpConnection(FEventLoop *loop, Socket s, FSocketAddr addrIn)
    : m_loop(loop), m_sock(new FSock(s)), m_addrIn(addrIn),
      m_event(std::make_shared<FEvent>(loop, s, loop->getUniqueIdInLoop())),
      inBuffer(std::make_unique<FBuffer>(1024)),
      outBuffer(std::make_unique<FBuffer>(1024)) {}

void FTcpConnection::sendInLoop(const char *data, uint64 len) {
  int sendLen = 0;
  if (!this->m_event->isWriting())
    return;

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
        m_loop->AddTask(std::bind(&FTcpConnection::handleWriteComplete, this));
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

} // namespace Fei