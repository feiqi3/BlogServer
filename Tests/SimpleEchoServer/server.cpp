#include "FCallBackDef.h"
#include "FDef.h"
#include "FEvent.h"
#include "FSocket.h"
#include "FeiLibIniter.h"
#include "FREventDef.h"
#include "FTCPConnection.h"
#include "FTCPServer.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include "FAcceptor.h"
#include "FBufferReader.h"
#include "FEPollListener.h"
#include "FEvent.h"
#include "FEventLoop.h"
#include "FTCPServer.h"
#include <chrono>
#include <thread>

void simple_echo();

void poll_echo();

void epoll_echo();

void eventLoop();

void FTcpServer_echo();

int main() {
    FTcpServer_echo();
  return 0;
}

void poll_echo() {
  using namespace Fei;
  FeiLibInit();
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
  FeiLibUnInit();
}

void simple_echo() {
  using namespace Fei;
  Socket socket;
  FeiLibInit();
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
    if (status != SocketStatus::Success) {
        printf("Close client failed: %s\n", StatusToStr(status));
        printf("Error: %s\n", GetErrorStr().c_str());
        break;
    }
    char sending[256] = {};
    auto len =  sprintf(sending, "Sending from server : hello client %d.%d.%d.%d : %d", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
        addr.un.un_byte.a3, addr.port);
    int writeLen = 0;
    Send(client, sending, len,writeLen);
    if (status != SocketStatus::Success) {
        printf("Close client failed: %s\n", StatusToStr(status));
        printf("Error: %s\n", GetErrorStr().c_str());
        break;
    }
    Recv(client, data, 127, RecvFlag::None, recLen);
    Close(client);
  }
  Close(socket);
  FeiLibUnInit();
}

void epoll_echo() {
  using namespace Fei;
  FeiLibInit();
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
  if (isEpollHandleValid(ephnd)) {
    printf("Create epoll failed: %s\n", GetErrorStr().c_str());
    return;
  }

  FEpollEvent event;
  event.events = EPOLLIN;
  event.data.sock = socket;
  int ret = EPollCtl(ephnd, EPollOp::Add, socket, &event);
  if (ret == -1) {
    printf("Add epoll failed: %s\n", GetErrorStr().c_str());
    return;
  }

  std::vector<FEpollEvent> readyEvents(36);
  std::map<Socket, FSocketAddr> addrSock;
  while (true) {
      int ret = EPollWait(ephnd, readyEvents.data(), 36, -1);
      if (ret == -1) {
          printf("Wait epoll failed: %s\n", GetErrorStr().c_str());
          break;
      }

      for (auto&& rEvent : readyEvents) {
          if (REvent::FromEpoll(rEvent.events) & REvent::In) {
              Socket client;
              FSocketAddr addr;
              status = Accept(rEvent.data.sock, client, &addr);
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
              char sending[256] = {};
              auto len = sprintf(sending, "Sending from server : hello client %d.%d.%d.%d : %d", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
                  addr.un.un_byte.a3, addr.port);
              int writeLen = 0;
              auto status = Send(client, sending, len, writeLen);
              if (writeLen < 0) {
                  std::cout << "Error";
              }
              status = Recv(client, sending,len, RecvFlag::None,recLen);

              Close(client);
          }
      }
  }
  EPollCtl(ephnd, EPollOp::Del, socket, nullptr);
  EPollClose(ephnd);
  FeiLibUnInit();
}

void eventLoop() {
  using namespace Fei;
  FeiLibInit();
  auto loop = new FEventLoop(std::make_unique<FEPollListener>());
  auto acceptor = std::make_unique<FAcceptor>(loop, "127.0.0.1", 12345, true);
  // Important: event's lifetime
  std::map<uint32, FEventPtr> mMap;
  FAcceptor::OnNewConnectionFunc func = [loop, &mMap](Socket s,
                                                      FSocketAddr addr) {
    auto event = FEvent::createEvent(loop, s, loop->getUniqueIdInLoop());
    mMap[s] = event;
    event->enableReading();
    event->enableWriting();
    auto onClose = [s, addr, loop, &mMap]() {
      printf("Closing client: %d.%d.%d.%d\n", addr.un.un_byte.a0,
             addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3);
      Close(s);
      mMap.erase(s);
    };
    auto onRead = [addr, s, onClose]() {
      char data[128];
      int len = 0;
      auto status = Recv(s, data, 127, RecvFlag::None, len);
      (void)status;
      if (len <= 0) {
        onClose();
        return;
      }
      printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
             addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
             addr.un.un_byte.a3, len, data);
    };
    event->setWriteCallback([s, addr, loop, &mMap]() {
        char sending[256];

        auto len = sprintf(sending, "Sending from server : hello client %d.%d.%d.%d : %d", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
            addr.un.un_byte.a3, addr.port);
        int writeLen = 0;

        Send(s, sending, len, writeLen);
        mMap[s]->disableWriting();
        });
    event->setReadCallback(onRead);

    event->setCloseCallback(onClose);
  };
  acceptor->SetOnNewConnCallback(func);
  loop->Loop();
  FeiLibUnInit();
}

void FTcpServer_echo() {
  using namespace Fei;
  FeiLibInit();
  FTcpServer *server = new FTcpServer(1);
  server->init();
  server->addListenPort(12345);
  server->setOnConnEstablisedCallback([](FTcpConnPtr ptr) {
    auto addr = ptr->getAddr();
    std::cout<<"use count "<<ptr.use_count()<<"\n";
    printf("Accept client: %d.%d.%d.%d\n", addr.un.un_byte.a0,
           addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3);
    ptr->setReading(true);
  });

  server->setOnMessageCallback([](FTcpConnPtr ptr, FBufferReader &buf) {
    auto len = buf.readTo(0, 0);
    std::vector<char> mBytes(len + 1);
    len = buf.readTo(mBytes.data(), len);
    auto addr = ptr->getAddr();
    std::cout<<"use count "<<ptr.use_count()<<"\n";
    printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
           addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
           addr.un.un_byte.a3, len, mBytes.data());
    char sending[256];

    len = sprintf(sending, "Sending from server : hello client %d.%d.%d.%d : %d", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
        addr.un.un_byte.a3, addr.port);
    int writeLen = 0;
    ptr->send(sending, len);
  });

  server->setOnCloseCallback([](FTcpConnPtr ptr) {
    (void)ptr;
    std::cout<<"use count "<<ptr.use_count()<<"\n";
    auto addr = ptr->getAddr();
    printf("Close client: %d.%d.%d.%d\n", addr.un.un_byte.a0,
           addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3);
  });
  server->run();
  while(1){
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);
  }
  FeiLibUnInit();
}