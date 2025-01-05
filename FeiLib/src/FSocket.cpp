#include "FSocket.h"
#include "FDef.h"
#include <cassert>
#include <io.h>
#include <string>
#include <winsock2.h>

#ifdef _WIN32
#include "wepoll.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
#pragma warning(suppress : 4996)

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

FSocketAddr::FSocketAddr(const char *ip, uint16 port) {
  this->port = ::htons(port);

#ifdef _WIN32
  inet_pton(AF_INET, ip, &un.un_addr);
#else
  un.un_addr = inet_addr(ip);
#endif
}

void FSocketAddr::toHumanFriendyType(char *buf, uint32 len, uint16 *port) {
  if (port) {
    *port = ntohs(this->port);
  }
  if (buf) {
    inet_ntop(AF_INET, (void *)&this->un, buf, len);
  }
}

SocketStatus Create(Socket &s) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) {
    return SocketStatus::CreateFailed;
  }
#else
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1) {
    return SocketStatus::CreateFailed;
  }
#endif
  return status;
}

SocketStatus Create(Socket &s, int type, int protocal) {
  SocketStatus status = SocketStatus::Success;
  int _type = 0;
  switch (type) {
  case SocketType::Dgram:
    _type = SOCK_DGRAM;
    break;
  case SocketType::Stream:
    _type = SOCK_STREAM;
    break;
  }
  int _protocal = 0;
  switch (protocal) {
  case SocketProt::TCP:
    _protocal = IPPROTO_TCP;
    break;
  case SocketProt::UDP:
    _protocal = IPPROTO_UDP;
    break;
  }

  s = socket(AF_INET, _type, _protocal);
#ifdef _WIN32
  if (s == INVALID_SOCKET) {
    return SocketStatus::CreateFailed;
  }
#else
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

SocketStatus Bind(Socket socket, const char *ip, uint16 port) {
  FSocketAddr addr(ip, port);
  return Bind(socket, addr);
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

SocketStatus Bind(Socket socket, FSocketAddr addr) {
  SocketStatus status = SocketStatus::Success;
#ifdef _WIN32
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = addr.port;
  addr_in.sin_addr.S_un.S_addr = addr.un.un_addr;
  int ret = bind(socket, (sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == SOCKET_ERROR) {
    status = SocketStatus::BindFailed;
  }
#else
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = addr.;
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
  addr->port = (addr_in.sin_port);
  if (client == INVALID_SOCKET) {
    status = SocketStatus::AcceptFailed;
  }
#else
  sockaddr_in addr_in{};
  int len_new = sizeof(sockaddr_in);
  client = accept(listen, &addr_in, &len_new);
  addr->un.un_addr = addr_in.sin_addr.s_addr;
  addr->port = (addr_in.sin_port);
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

SocketStatus Connect(Socket socket, const char *ip, uint16 port) {
  FSocketAddr addr(ip, port);
  return Connect(socket, addr);
}

SocketStatus Connect(Socket socket, FSocketAddr addr) {
  SocketStatus status = SocketStatus::Success;
  sockaddr_in addr_in{};
  addr_in.sin_family = AF_INET;
  addr_in.sin_port = addr.port;
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

long SendV(Socket socket, iovec *iov, int count) {
#ifdef _WIN32
  long totallen = 0, tlen = -1;
  while (count) {
    tlen = send(socket, (const char *)iov->iov_base, iov->iov_len, 0);
    if (tlen < 0)
      return tlen;
    totallen += tlen;
    iov++;
    count--;
  }

  return totallen;

#else
  return sendV(socket, (::iovec *)iov, count);
#endif
}
int Readv(Socket socket, struct iovec *iov, int count) {
#ifdef _WIN32
  long r, t = 0;
  while (count) {
    r = read(socket, iov->iov_base, iov->iov_len);
    if (r < 0)
      return r;
    t += r;
    iov++;
    count--;
  }
  return t;
#else
  return readv(socket, (::iovec *)iov, count);
#endif
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
  // under win32, only support Level Triggered
  return epoll_create1(flags);
}

int SetSockOpt(Socket s, SockOpt opt, bool on) {
  int _opt = 0;
  if (opt == SockOpt::ReuseAddr) {
    _opt = SO_REUSEADDR;
  } else if (opt == SockOpt::ReusePort) {
#ifndef _WIN32
    _opt = SO_REUSEPORT;
#else
    return 0;
#endif
  } else if (opt == SockOpt::KeepAlive) {
    _opt = SO_KEEPALIVE;
  }

  if (_opt != 0)
    return setsockopt(s, SOL_SOCKET, _opt, on ? (char *)1 : 0, sizeof(_opt));

//-------------------------------------------//
#ifdef _WIN32
  if (opt == SockOpt::NoneBlock || opt == SockOpt::NoneBlockAndCloseOnExec) {
    unsigned long _temp = 1;
    return ioctlsocket(s, FIONBIO, &_temp);
  } else {
    return 0;
    // No close on exec in windows
  }
#else

  if (opt == SockOpt::NoneBlock) {
    int flags = ::fcntl(s, F_GETFL, 0);
    flags |= O_NONBLOCK;
    return ::fcntl(s, F_SETFL, flags);
  } else if (opt == SockOpt::CloseOnExec) {
    int flags = ::fcntl(s, F_GETFL, 0);
    flags |= FD_CLOEXEC;
    return ::fcntl(s, F_SETFL, flags);
  } else if (opt == SockOpt::NoneBlockAndCloseOnExec) {
    int flags = ::fcntl(s, F_GETFL, 0);
    flags |= O_NONBLOCK;
    int ret = ::fcntl(s, F_SETFL, flags);
    flags = ::fcntl(s, F_GETFL, 0);
    flags |= FD_CLOEXEC;
    return ::fcntl(s, F_SETFL, flags);
  }

#endif
}

}; // namespace Fei
