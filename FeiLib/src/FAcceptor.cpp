#include "FAcceptor.h"
#include "FDef.h"
#include "FEvent.h"
#include "FSockWrapper.h"
#include "FSocket.h"

#include "FEventLoop.h"
#include "FLogger.h"

#include <cassert>
#include <cstdlib>
#include <functional>
#include <memory>
namespace Fei {

FAcceptor::FAcceptor(FEventLoop *loop, const char *listenAddr, int port,
                     bool reusePort)
    : m_loop(loop), m_listenAddr(listenAddr), m_listenPort(port) {
  Socket socket;
  auto status = Create(socket, SocketType::Stream, SocketProt::TCP);
  if (status != SocketStatus::Success) {
    Logger::instance()->log("Acceptor", lvl::critical,
                            "Socket create failed, reason: {}", GetErrorStr());
  }
  assert(status == SocketStatus::Success);
  m_sock = FSock(socket); // For a possible copy elision...
  m_sock.setReuseport(reusePort);
  m_sock.setNoneBlock(true);
  m_sock.setExitOnExec(true);
  m_event =
      FEvent::createEvent(m_loop, m_sock.getFd(), loop->getUniqueIdInLoop());
  m_event->enableReading();
  m_addr = FSocketAddr(listenAddr, port);
  status = Bind(m_sock.getFd(), m_addr);
  if (status != SocketStatus::Success) {
    Logger::instance()->log("Acceptor", lvl::critical,
                            "Socket bind failed, reason: {}", GetErrorStr());
  }
  status = Listen(m_sock.getFd(), 1024);
  if (status != SocketStatus::Success) {
    Logger::instance()->log("Acceptor", lvl::critical,
                            "Socket listen failed, reason: {}", GetErrorStr());
  }
  m_event->setReadCallback(std::bind(&FAcceptor::handleRead, this));

  assert(status == SocketStatus::Success);
}

void FAcceptor::handleRead() {
  Socket socket;
  FSocketAddr addrNew;
  auto status = Accept(m_sock.getFd(), socket, &addrNew);
  if (status == SocketStatus::Success) {
    if (_newConnCb) {
      _newConnCb(socket, addrNew);
    } else {
      Close(socket);
    }
  } else {
    // Accept error;
    Logger::instance()->log("Acceptor", lvl::err,
                            "Socket accept failed, reason: {}", GetErrorStr());
  }
}

FAcceptor::~FAcceptor() {
  m_loop->RemoveEvent(m_event.get());
  m_event = nullptr;
}

} // namespace Fei