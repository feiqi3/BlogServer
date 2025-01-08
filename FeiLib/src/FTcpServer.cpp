#include <algorithm>
#include <cassert>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "FAcceptor.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FSocket.h"
#include "FTCPConnection.h"
#include "FTCPServer.h"

namespace Fei {
TcpServer::TcpServer(uint32 threadNum)
    : m_listenerLoop(
          std::make_unique<FEventLoop>(std::make_unique<FEPollListener>())),
      m_threadNums(threadNum) {}
void TcpServer::init() {
  m_threadNums = std::max(m_threadNums, 1u);
  {
    std::thread _mainLoop([this]() { m_listenerLoop->Loop(); });
    _mainLoop.detach();
  }
  m_subLoops.reserve(m_threadNums);
  for (auto i = 0u; i < m_threadNums; ++i) {
    m_subLoops.emplace_back(
        std::make_unique<FEventLoop>(std::make_unique<FEPollListener>()));
    {
      std::thread _Loop([this, i]() { m_subLoops[i]->Loop(); });
      _Loop.detach();
    }
  }
}

void TcpServer::addListenPort(uint32 port) {
  m_acceptors.emplace_back(std::make_unique<FAcceptor>(m_listenerLoop.get(),
                                                       inAddrAny, port, false));
}
void TcpServer::removeListenPort(uint32 port) {
  std::erase_if(m_acceptors, [port](auto &in) {
    if (in->getListenPort() == port)
      return true;
    else
      return false;
  });
  m_acceptors.resize(m_acceptors.size() - 1);
}

void TcpServer::onNewConnIn(Socket inSock, FSocketAddr addr) {
  auto ptr = FTcpConnection::makeConn(m_listenerLoop.get(), inSock, addr);
  //TODO: set more cb
  this->mOnEstablishedCallback(ptr);
}

} // namespace Fei