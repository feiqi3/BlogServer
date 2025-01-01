#include "FSocket.h"
#include "FDef.h"
#include <cassert>
#include <string>
#include <winsock2.h>

#ifdef _WIN32
#include "wepoll.h"
#include <WS2tcpip.h>
#include <WinSock2.h>

#elif defined(__linux__)
#include "poll.h"
#include <sys/socket.h>

#elif defined(__APPLE__)
#include "poll.h"
#include <sys/socket.h>

#endif

namespace Fei {
namespace {
#ifdef _WIN32
WSAData g_wsaData;
#endif

constexpr const char *SocketStatusStr[] = {
    "Success",      "Fail",         "CreateFailed",  "BindFailed",
    "ListenFailed", "AcceptFailed", "ConnectFailed", "SendFailed",
    "RecvFailed",   "CloseFailed"};

int recvFlagToPlatfromSpec(RecvFlag flag) {
#ifdef _WIN32
  switch (flag) {

  case RecvFlag::None:
    return 0;
  case RecvFlag::Peek:
    return MSG_PEEK;
  case RecvFlag::WaitAll:
    return MSG_WAITALL;
  case RecvFlag::DontWait:
    return -10; // MAGIC NUMBER
  default:
    return 0;
  }
#else
  switch (flag) {
  case RecvFlag::None:
    return 0;
  case RecvFlag::Peek:
    return MSG_PEEK;
  case RecvFlag::WaitAll:
    return MSG_WAITALL;
  case RecvFlag::DontWait:
    return MSG_DONTWAIT;
  default:
    return 0;
  }
#endif
}

} // namespace

const char *StatusToStr(SocketStatus status) {
  assert((uint32)status <= uint32(SocketStatus::MAX_STATUS));
  return SocketStatusStr[static_cast<int>(status)];
}

SocketStatus Create(Socket &s) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) {
    return SocketStatus::CreateFailed;
  }
#elif defined(__linux__)
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1) {
    return SocketStatus::CreateFailed;
  }
#elif defined(__APPLE__)
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1) {
    return SocketStatus::CreateFailed;
  }
#endif
  return status;
}

SocketStatus FeiInit() {
#ifdef _WIN32
  int ret = WSAStartup(MAKEWORD(2, 2), &g_wsaData);
  if (ret != 0) {
    return SocketStatus::Fail;
  }
#elif defined(__linux__)
#elif defined(__APPLE__)
#endif
  return SocketStatus::Success;
}

SocketStatus FeiUnInit() {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  int ret = WSACleanup();
  if (ret != 0) {
    status = SocketStatus::Fail;
  }
#elif defined(__linux__)
#elif defined(__APPLE__)
#endif
  return status;
}

SocketStatus Bind(Socket socket, const char *ip, int port) {
  FSocketAddr addr = to_addr(ip);
  return Bind(socket, addr, port);
}

SocketStatus Listen(Socket socket, int backlog) {

  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  int ret = listen(socket, backlog);
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::ListenFailed;
  }
#else
  int ret = listen(socket, backlog);
  if (ret == -1) {
    status = SocketStatus::ListenFailed;
  }
#endif
  return status;
}

FSocketAddr to_addr(const char *ip) {
  FSocketAddr addr{};
#ifdef _WIN32
  inet_pton(AF_INET, ip, &addr.un.un_addr);
#else
  addr.un.un_addr = inet_addr(ip);
#endif
  return addr;
}

SocketStatus Bind(Socket socket, FSocketAddr addr, int port) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = htons(port);
  addr_in.sin_addr.S_un.S_addr = addr.un.un_addr;
  int ret = bind(socket, (sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::BindFailed;
  }
#else
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = htons(port);
  addr_in.sin_addr.s_addr = addr.un.un_addr;
  int ret = bind(socket, (sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == -1) {
    status = SocketStatus::BindFailed;
  }
#endif
  return status;
}

SocketStatus Accept(Socket listen, Socket &client, FSocketAddr *addr) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  sockaddr_in addr_in{};
  int len_new = sizeof(sockaddr_in);
  client = accept(listen, (sockaddr *)&addr_in, &len_new);
  addr->un.un_addr = addr_in.sin_addr.S_un.S_addr;

  if (client == INVALID_SOCKET) {
    status = SocketStatus::AcceptFailed;
  }
#else
  sockaddr_in addr_in{};
  int len_new = sizeof(sockaddr_in);
  client = accept(listen, &addr_in, &len_new);
  addr->un.un_addr = addr_in.sin_addr.s_addr;
  if (client == -1) {
    status = SocketStatus::AcceptFailed;
  }
#endif
  return status;
}

SocketStatus Send(Socket socket, const char *data, int len) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  int ret = send(socket, data, len, 0);
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::SendFailed;
  }
#else
  int ret = send(socket, data, len, 0);
  if (ret == -1) {
    status = SocketStatus::SendFailed;
  }
#endif
  return status;
}

SocketStatus Recv(Socket socket, char *data, int len, RecvFlag flag,
                  int &recv_len) {
  SocketStatus status = SocketStatus::Success;
  int flag_ = recvFlagToPlatfromSpec(flag);
#ifdef _WIN32
  // Under win32, no MSG_DONTWAIT, so we use ioctlsocket to set non-blocking
  if (flag_ == -10) {
    flag_ = 0;
    u_long unblockFlag = 1;
    ioctlsocket(socket, FIONBIO, (u_long *)&unblockFlag);
  }
  int ret = recv(socket, data, len, flag_);
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::RecvFailed;
  }
  recv_len = ret;
#else
  int ret = recv(socket, data, len, flag_);
  if (ret == -1) {
    status = SocketStatus::RecvFailed;
  }
  recv_len = ret;
#endif
  return status;
}

SocketStatus Close(Socket socket) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  int ret = closesocket(socket);
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::CloseFailed;
  }
#else
  int ret = close(socket);
  if (ret == -1) {
    status = SocketStatus::CloseFailed;
  }
#endif
  return status;
}

SocketStatus Connect(Socket socket, const char *ip, int port) {
  FSocketAddr addr = to_addr(ip);
  return Connect(socket, addr, port);
}

SocketStatus Connect(Socket socket, FSocketAddr addr, int port) {
  SocketStatus status = SocketStatus::Success;
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = htons(port);
  addr_in.sin_addr.s_addr = addr.un.un_addr;
#ifdef _WIN32
  int ret = connect(socket, (sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::ConnectFailed;
  }
#else
  int ret = connect(socket, (sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == -1) {
    status = SocketStatus::ConnectFailed;
  }
#endif
  return status;
}

std::string GetErrorStr() {
#ifdef _WIN32
  char *msgBuffer = nullptr;
  int errorCode = WSAGetLastError();
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPSTR)&msgBuffer, 0, nullptr);
  std::string errorMsg(msgBuffer);
  LocalFree(msgBuffer); // 释放缓冲区

  return errorMsg;
#else
  return std::string(strerror(errno));
#endif
}

int FPoll(FPollfd *sockets, int num, int timeout) {
#ifdef _WIN32
  return WSAPoll((pollfd *)sockets, num, timeout);
#elif defined(__linux__) or defined(__APPLE__)
  return poll((pollfd *)sockets, num, timeout);
#endif
}

EpollHandle EPollCreate(int size) { return epoll_create(size); }

int EPollCtl(EpollHandle ephnd, EPollOp op, Socket sock, FEpollEvent *event) {
  return epoll_ctl(ephnd, (int)op, sock, (epoll_event *)event);
}

int EPollClose(EpollHandle ephnd) { return epoll_close(ephnd); }

int EPollWait(EpollHandle ephnd, FEpollEvent *events, int maxevents,
              int timeout) {
  return epoll_wait(ephnd, (epoll_event *)events, maxevents, timeout);
}

EpollHandle EPollCreate1(int flags) {
#ifdef _WIN32
  flags = 0;
#endif
//under win32, only support Level Triggered
  return epoll_create1(flags);
}

}; // namespace Fei
