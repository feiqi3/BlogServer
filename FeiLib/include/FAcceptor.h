#pragma once

#include "FDef.h"
#include "FListener.h"
#include "FNoCopyable.h"
#include "FSockWrapper.h"

#include <functional>
namespace Fei {
class Acceptor : public FNoCopyable {
public:
  using OnNewConnectionFunc = std::function<void(Socket,FSocketAddr)>;

public:
  Acceptor(class FListener *listener, const char *listenAddr,int port, bool reusePort);
  ~Acceptor();

  void SetOnNewConnCallback(OnNewConnectionFunc func);
  void listen();

private:
    void handleRead();

private:
  FSock m_sock;
  class FEvent *m_event;
  FSocketAddr m_addr;

  OnNewConnectionFunc _newConnCb = nullptr;
};
} // namespace Fei