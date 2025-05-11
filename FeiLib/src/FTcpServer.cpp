#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "FAcceptor.h"
#include "FCallBackDef.h"
#include "FConfigReader.h"
#include "FDef.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSSLHelper.h"
#include "FSocket.h"
#include "FTCPConnection.h"
#include "FTCPServer.h"
#include <functional>

namespace Fei {
FTcpServer::FTcpServer(uint32 threadNum)
    : m_listenerLoop(
          std::make_unique<FEventLoop>(std::make_unique<FEPollListener>())),
      m_threadNums(threadNum) {}

FTcpServer::~FTcpServer() { stop(true); }

void FTcpServer::init() {
  m_threadNums = std::max(m_threadNums, 1u);

  //* read config *//
  const auto cfg = FConfigReader::instance();
  auto tcpIdle = cfg->getCfg("TcpIdleTime");
  if(tcpIdle.has_value()){
    int idleTime = -1;
    if(FCfgUtils::toNumber(tcpIdle.value(), idleTime)){
      mTcpConnIdleTime = idleTime;
    }
  }

  auto socketKeepAlive = cfg->getCfg("SocketKeepAliveTime");
  if(socketKeepAlive.has_value()){
    int idleTime = -1;
    if(FCfgUtils::toNumber(socketKeepAlive.value(), idleTime)){
      mSocketKeepAlive = idleTime;
    }
  }
}

void FTcpServer::deinitGlobalSSLEnv() {
  if (FSSLEnv::valid())
    delete FSSLEnv::instance();
}

void FTcpServer::initGlobalSSLEnv(const std::string &certificateFile,
                                  const std::string &privateKeyFile) {
  if (!FSSLEnv::valid()) {
    new FSSLEnv(certificateFile, privateKeyFile);
    if (!FSSLEnv::instance()->isEnvSetup()) {
      delete FSSLEnv::instance();
    }
  } else
    throw std::runtime_error("Double init SSL Environment");
}

void FTcpServer::run() {
  if (m_running)
    return;
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

void FTcpServer::stop(bool forceClose) {
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
  
  m_tcpConns.clear();

  for (auto &&loop : m_subLoops) {
    if (forceClose)
      loop->ForceQuit();
    else
      loop->Quit();
  }
  for (auto &&loop : m_subLoops) {
    while (!loop->HasStoped()){
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }; 
      toCloseNums--;
  }
  m_running = false;
}

void FTcpServer::addSslListenPort(uint32 port, bool reuseport) {
  if (!FSSLEnv::valid() || !FSSLEnv::instance()->isEnvSetup()) {
    Logger::instance()->log(lvl::err, "[FTcpServer]SSL Environment not setup");
    return;
  }
  addListenPort(port, reuseport);
  m_sslPort.push_back(port);
}
void FTcpServer::addListenPort(uint32 port, bool reuseport) {
  auto acc = std::make_unique<FAcceptor>(m_listenerLoop.get(), inAddrAny, port,
                                         reuseport);
  auto functor =
      (std::bind(&FTcpServer::onNewConnIn, this, std::placeholders::_1,
                 std::placeholders::_2, std::placeholders::_3));
  acc->SetOnNewConnCallback(functor);
  m_acceptors.emplace_back(std::move(acc));
}
void FTcpServer::removeListenPort(uint32 port) {
  std::erase_if(m_acceptors, [port](auto &in) {
    if (in->getListenPort() == port)
      return true;
    else
      return false;
  });
  m_acceptors.resize(m_acceptors.size() - 1);

  std::erase_if(m_sslPort, [port](auto &in) {
    if (in == port)
      return true;
    else
      return false;
  });
  m_sslPort.resize(m_sslPort.size() - 1);
}
void FTcpServer::onClose(FTcpConnPtr ptr) {
  {
    FAUTO_LOCK(m_mutex);
    m_tcpConns.erase(ptr->getFd());
  }
  mOnCloseCallback(ptr);
}

void FTcpServer::onNewConnIn(Socket inSock, FSocketAddr addr,
                             FSocketAddr addrAccept) {

  auto choosenLoop = m_subLoops[IOThread_Chooser++].get();
  IOThread_Chooser = IOThread_Chooser % m_subLoops.size();
  bool isSSL = false;
  for (auto &&sslPort : this->m_sslPort) {
    if (sslPort == addrAccept.getPort()) {
      isSSL = true;
      break;
    }
  }
  auto ptr =
      FTcpConnection::makeConn(choosenLoop, inSock, addr, addrAccept, isSSL);
  ptr->setCloseCallback(
      std::bind(&FTcpServer::onClose, this, std::placeholders::_1));
  ptr->setMessageCallback(mOnMessageCallback);
  ptr->setWriteCompleteCallback(mWriteCompleteCallback);

  if(mOnIdleCallback){
    ptr->setIdleCallback(mOnIdleCallback, mTcpConnIdleTime);
  }

  if(mSocketKeepAlive > 0){
    ptr->setKeepAlive(true);
    ptr->setKeepIdle(mSocketKeepAlive);
  }
  choosenLoop->AddTask(std::bind(mOnEstablishedCallback, ptr));
  {
    FAUTO_LOCK(m_mutex);
    m_tcpConns.insert({inSock, ptr});
  } // TODO: set more cb
  // this->mOnEstablishedCallback(ptr);
}

TickEventId FTcpServer::addTickEvent(AppTickEvent event) {
  FAUTO_LOCK(m_tickEventMutex);
  auto id = m_tickEventId++;
  m_tickEvents.insert({id, std::move(event)});
  return id;
}

void FTcpServer::removeEvent(TickEventId id) {
  FAUTO_LOCK(m_tickEventMutex);
  auto itor = m_tickEvents.find(id);
  if (itor == m_tickEvents.end())
    return;
  m_tickEvents.erase(itor);
}

void FTcpServer::tickUserEvent() {
  FAUTO_LOCK(m_tickEventMutex);
  uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  for (auto &&i : m_tickEvents) {
    i.second(timeNow);
  }
}

} // namespace Fei