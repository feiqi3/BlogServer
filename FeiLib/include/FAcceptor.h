#pragma once

#include "FDef.h"
#include "FListener.h"
#include "FNoCopyable.h"
#include "FSockWrapper.h"

#include <functional>
namespace Fei {
class F_API FAcceptor : public FNoCopyable {
public:
  using OnNewConnectionFunc = std::function<void(Socket,FSocketAddr)>;

public:
  FAcceptor(class FEventLoop *loop, const char *listenAddr,int port, bool reusePort);
  ~FAcceptor();

  void SetOnNewConnCallback(OnNewConnectionFunc func){_newConnCb = std::move(func);}
  void listen();

private:
    void handleRead();

private:
FEventLoop* m_loop;
  FSock m_sock;
  class FEvent *m_event;
  FSocketAddr m_addr;
  OnNewConnectionFunc _newConnCb = nullptr;
};
} // namespace Fei