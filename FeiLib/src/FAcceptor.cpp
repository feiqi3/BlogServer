#include "FAcceptor.h"
#include "FDef.h"
#include "FEvent.h"
#include "FSocket.h"
#include <cassert>
#include <functional>
namespace Fei {

Acceptor::Acceptor(class FListener *listener, const char *listenAddr, int port,
                   bool reusePort) {
  Socket socket;
  auto status = Create(socket, SocketType::Stream, SocketProt::TCP);
  assert(status == SocketStatus::Success);
  m_sock = FSock(socket); // For a possible copy elision...
  m_sock.setReuseport(reusePort);
  m_sock.setNoneBlock(true);
  m_sock.setExitOnExec(true);
  m_event = new FEvent(listener, m_sock.getFd(), listener->getId());
  m_addr = FSocketAddr(listenAddr, port);
  status = Bind(m_sock.getFd(), m_addr);

  m_event->setReadCallback(std::bind(&Acceptor::handleRead,this));

  assert(status == SocketStatus::Success);
}

void Acceptor::handleRead() {
  Socket socket;
  FSocketAddr addrNew;
  auto status = Accept(m_sock.getFd(), socket, &addrNew);
  if(status == SocketStatus::Success){
    if(_newConnCb){
        _newConnCb(socket,addrNew);
    }else{
        Close(socket);
    }
  }else{
    //Accept error;
  }
}

} // namespace Fei