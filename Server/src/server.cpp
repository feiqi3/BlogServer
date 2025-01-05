#include "FEvent.h"
#include "FListener.h"
#include "FSocket.h"
#include "FeiLib/FSocket.h"
#include "cstdio"
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include "FAcceptor.h"
#include "FEPollListener.h"
#include "FEvent.h"
#include "FEventLoop.h"

void simple_echo();

void poll_echo();

void epoll_echo();

void eventLoop();

int main() {
  eventLoop();
  return 0;
}

void poll_echo() {
  using namespace Fei;
  FeiInit();
  Socket socket;
  SocketStatus status = Fei::SocketStatus::Success;

  status = Create(socket);
  if (status != SocketStatus::Success) {
    printf("Create socket failed: %s\n", StatusToStr(status));
    return;
  }

  status = Bind(socket, Fei::inAddrAny, 12345);
  if (status != SocketStatus::Success) {
    printf("Bind failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return;
  }

  status = Listen(socket, 100);
  if (status != SocketStatus::Success) {
    printf("Listen failed: %s\n", StatusToStr(status));
    return;
  }

  std::vector<FPollfd> pollfds;
  pollfds.push_back({socket, POLLRDNORM, 0});

  while (1) {
    int ret = FPoll(pollfds.data(), pollfds.size(), -1);
    if (ret == -1) {
      printf("Poll failed: %s\n", GetErrorStr().c_str());
      break;
    }
    for (auto &pollfd : pollfds) {
      if (pollfd.revents & POLLRDNORM) {
        FSocketAddr addr;
        char data[128] = {};
        Socket client;
        status = Accept(socket, client, &addr);
        if (status != SocketStatus::Success) {
          printf("Accept failed: %s\n", StatusToStr(status));
          printf("Error: %s\n", GetErrorStr().c_str());
          break;
        }
        int recLen = 0;
        status = Recv(client, data, 127, RecvFlag::None, recLen);
        printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
               addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
               addr.un.un_byte.a3, recLen, data);
        Close(client);
        if (status != SocketStatus::Success) {
          printf("Close client failed: %s\n", StatusToStr(status));
          printf("Error: %s\n", GetErrorStr().c_str());
          break;
        }
      }
    }
  }
  FeiUnInit();
}

void simple_echo() {
  using namespace Fei;
  Socket socket;
  FeiInit();
  SocketStatus status = Create(socket);
  if (status != SocketStatus::Success) {
    printf("Create socket failed: %s\n", StatusToStr(status));
  }
  status = Bind(socket, "0.0.0.0", 12345);
  if (status != SocketStatus::Success) {
    printf("Bind failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
  }
  status = Listen(socket, 5);
  if (status != SocketStatus::Success) {
    printf("Listen failed: %s\n", StatusToStr(status));
  }
  while (1) {
    FSocketAddr addr;
    char data[128] = {};
    Socket client;
    status = Accept(socket, client, &addr);
    if (status != SocketStatus::Success) {
      printf("Accept failed: %s\n", StatusToStr(status));
      printf("Error: %s\n", GetErrorStr().c_str());
      break;
    }
    int recLen = 0;
    status = Recv(client, data, 127, RecvFlag::None, recLen);
    printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
           addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
           addr.un.un_byte.a3, recLen, data);
    Close(client);
    if (status != SocketStatus::Success) {
      printf("Close client failed: %s\n", StatusToStr(status));
      printf("Error: %s\n", GetErrorStr().c_str());
      break;
    }
  }
  Close(socket);
  FeiUnInit();
}

void epoll_echo() {
  using namespace Fei;
  FeiInit();
  Socket socket;
  SocketStatus status = Fei::SocketStatus::Success;
  status = Create(socket);
  if (status != SocketStatus::Success) {
    printf("Create socket failed: %s\n", StatusToStr(status));
    return;
  }

  status = Bind(socket, Fei::inAddrAny, 12345);
  if (status != SocketStatus::Success) {
    printf("Bind failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return;
  }

  status = Listen(socket, 100);

  if (status != SocketStatus::Success) {
    printf("Listen failed: %s\n", StatusToStr(status));
    return;
  }

  EpollHandle ephnd = EPollCreate(100);
  if (ephnd == nullptr) {
    printf("Create epoll failed: %s\n", GetErrorStr().c_str());
    return;
  }

  FEpollEvent event;
  event.events = EPOLLIN;
  event.data.ptr = nullptr;
  int ret = EPollCtl(ephnd, EPollOp::Add, socket, &event);
  if (ret == -1) {
    printf("Add epoll failed: %s\n", GetErrorStr().c_str());
    return;
  }

  while (true) {
    int ret = EPollWait(ephnd, &event, 1, -1);
    if (ret == -1) {
      printf("Wait epoll failed: %s\n", GetErrorStr().c_str());
      break;
    }

    Socket client;
    FSocketAddr addr;
    status = Accept(socket, client, &addr);
    if (status != SocketStatus::Success) {
      printf("Accept failed: %s\n", StatusToStr(status));
      printf("Error: %s\n", GetErrorStr().c_str());
      break;
    }
    char data[128] = {};
    int recLen = 0;
    status = Recv(client, data, 127, RecvFlag::None, recLen);
    if (status != SocketStatus::Success) {
      printf("Recv failed: %s\n", StatusToStr(status));
      printf("Error: %s\n", GetErrorStr().c_str());
      break;
    }

    printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
           addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
           addr.un.un_byte.a3, recLen, data);
    Close(client);
  }
  EPollCtl(ephnd, EPollOp::Del, socket, nullptr);
  EPollClose(ephnd);
  FeiUnInit();
}

void eventLoop() {
  using namespace Fei;
  FeiInit();
  FEventLoop *loop = new FEventLoop(std::make_unique<FEPollListener>());
  auto acceptor = std::make_unique<FAcceptor>(loop, "127.0.0.1", 12345, true);
  FAcceptor::OnNewConnectionFunc func = [loop](Socket s, FSocketAddr addr) {
    FEvent *event = new FEvent(loop, s, loop->getUniqueIdInLoop());
    event->enableReading();
    auto onClose = [s, addr, event]() {
      printf("Closing client: %d.%d.%d.%d\n", addr.un.un_byte.a0,
             addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3);
      Close(s);
      delete event;
    };
    auto onRead = [addr, s,onClose]() {
      char data[128];
      int len = 0;
      auto status = Recv(s, data, 127, RecvFlag::None, len);
      if (len <= 0) {
        onClose();
        return;
      }
      printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
             addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
             addr.un.un_byte.a3, len, data);
      
    };
    event->setReadCallback(onRead);

    event->setCloseCallback(onClose);
  };
  acceptor->SetOnNewConnCallback(func);
  loop->Loop();
  FeiUnInit();
}