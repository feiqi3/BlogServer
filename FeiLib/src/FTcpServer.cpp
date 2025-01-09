#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "FAcceptor.h"
#include "FCallBackDef.h"
#include "FDef.h"
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
void TcpServer::init() { m_threadNums = std::max(m_threadNums, 1u); }

void TcpServer::run() {
  m_running = true;
  {
    std::thread _mainLoop([this]() { m_listenerLoop->Loop(); });
    _mainLoop.detach();
  }
  for (auto i = 0u; i < m_threadNums; ++i) {
    m_subLoops.emplace_back(
        std::make_unique<FEventLoop>(std::make_unique<FEPollListener>()));
    {
      std::thread _Loop([this, i]() { m_subLoops[i]->Loop(); });
      _Loop.detach();
    }
  }
}

void TcpServer::stop(bool forceClose) {
  if (!m_running)
    return;
  uint32 toCloseNums = m_subLoops.size();
  if (forceClose) {
    m_listenerLoop->ForceQuit();
  } else {
    this->m_listenerLoop->Quit();
  }

  while (!m_listenerLoop->HasStoped())
    ;

  for (auto &&loop : m_subLoops) {
    if (forceClose)
      loop->ForceQuit();
    else
      loop->Quit();
  }

  while (toCloseNums > 0) {
    for (auto i = 0u; i < toCloseNums;) {
      auto eraseSize = std::erase_if(m_subLoops, [](auto &in) {
        if (in->HasStoped()) {
          return true;
        }
        return false;
      });
      m_subLoops.resize(m_subLoops.size() - eraseSize);
      toCloseNums -= eraseSize;
    }
  }
  m_running = false;
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
void TcpServer::onClose(FTcpConnPtr ptr) {
  {
    FAUTO_LOCK(m_mutex);
    m_tcpConns.erase(ptr->getFd());
  }
  mOnCloseCallback(ptr);
}

void TcpServer::onNewConnIn(Socket inSock, FSocketAddr addr) {
  auto choosenLoop = m_subLoops[IOThread_Chooser++].get();
  auto ptr = FTcpConnection::makeConn(choosenLoop, inSock, addr);
  ptr->setCloseCallback(mOnCloseCallback);
  ptr->setMessageCallback(mOnMessageCallback);
  ptr->setWriteCompleteCallback(mWriteCompleteCallback);
  choosenLoop->AddTask(std::bind(mOnEstablishedCallback, ptr));
  {
    FAUTO_LOCK(m_mutex);
    m_tcpConns.insert({inSock, ptr});
  } // TODO: set more cb
  // this->mOnEstablishedCallback(ptr);
}

} // namespace Fei