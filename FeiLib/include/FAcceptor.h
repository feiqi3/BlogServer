#pragma once

#include "FDef.h"
#include "FListener.h"
#include "FNoCopyable.h"
#include "FSockWrapper.h"

#include <functional>
#include <string>
namespace Fei {
class F_API FAcceptor : public FNoCopyable {
public:
  using OnNewConnectionFunc = std::function<void(Socket,FSocketAddr,FSocketAddr)>;

public:
  FAcceptor(class FEventLoop *loop, const char *listenAddr,int port, bool reusePort);
  ~FAcceptor();

  void SetOnNewConnCallback(OnNewConnectionFunc func){_newConnCb = std::move(func);}

  std::string getListenAddr()const{return m_listenAddr;}
  uint32 getListenPort()const{return m_listenPort;}
private:
    void handleRead();

private:
  FEventLoop* m_loop;
  FSock m_sock;
  FEventPtr m_event;
  FSocketAddr m_addr;
  OnNewConnectionFunc _newConnCb = nullptr;
  std::string m_listenAddr;
  uint32 m_listenPort;
};
} // namespace Fei