#ifndef FSOCKET_H
#define FSOCKET_H
#include "FDef.h"
#include <string>

namespace Fei {

using Socket = uint64;

enum class SocketStatus {
  Success,
  Fail,
  CreateFailed,
  BindFailed,
  ListenFailed,
  AcceptFailed,
  ConnectFailed,
  SendFailed,
  RecvFailed,
  CloseFailed,
  MAX_STATUS = CloseFailed
};

enum class RecvFlag {
  None = 0,
  Peek = 1,
  WaitAll = 2,
  DontWait = 3,
  MAX_FLAG = DontWait
};

F_API const char *StatusToStr(SocketStatus status);

struct F_API FSocketAddr {
  union {
    struct {
      uint8 a0;
      uint8 a1;
      uint8 a2;
      uint8 a3;
    } un_byte;
    uint32 un_addr;
  } un;
};

struct F_API FPollfd {
  Socket fd;
  short events;
  short revents;
};

F_API FSocketAddr to_addr(const char *ip);

F_API SocketStatus FeiInit();
F_API SocketStatus FeiUnInit();
F_API SocketStatus Create(Socket &socket);
F_API SocketStatus Close(Socket socket);
F_API SocketStatus Connect(Socket socket, const char *ip, int port);
F_API SocketStatus Connect(Socket socket, FSocketAddr addr, int port);
F_API SocketStatus Bind(Socket socket, const char *ip, int port);
F_API SocketStatus Bind(Socket socket, FSocketAddr addr, int port);
F_API SocketStatus Listen(Socket socket, int backlog);
F_API SocketStatus Accept(Socket listen, Socket &client, FSocketAddr *addr);
F_API SocketStatus Send(Socket socket, const char *data, int len);
F_API SocketStatus Recv(Socket socket, char *data, int len, RecvFlag flag,
                        int &recv_len);
F_API std::string GetErrorStr();

F_API int FPoll(FPollfd *sockets, int num, int timeout);

inline const char *inAddrAny = "0.0.0.0";
inline const char *inAddrLoopback = "127.0.0.1";
inline const char *inAddrBroadcast = "255.255.255.255";

#ifdef _WIN32
const short POLLRDNORM = 0x0100;
const short POLLRDBAND = 0x0200;
const short POLLIN = (POLLRDNORM | POLLRDBAND);
const short POLLPRI = 0x0400;

const short POLLWRNORM = 0x0010;
const short POLLOUT = (POLLWRNORM);
const short POLLWRBAND = 0x0020;

const short POLLERR = 0x0001;
const short POLLHUP = 0x0002;
const short POLLNVAL = 0x0004;

#else

#endif

using EpollHandle = void *;

typedef union FEpollData {
  void *ptr;
  int i32;
  uint32_t u32;
  uint64_t u64;
  Socket sock;  
} epoll_data_t;

const int EPOLLIN = (int)(1U << 0),
            EPOLLPRI = (int)(1U << 1),
            EPOLLOUT = (int)(1U << 2), 
            EPOLLERR = (int)(1U << 3),
            EPOLLHUP = (int)(1U << 4), 
            EPOLLRDNORM = (int)(1U << 6),
            EPOLLRDBAND = (int)(1U << 7), 
            EPOLLWRNORM = (int)(1U << 8),
            EPOLLWRBAND = (int)(1U << 9),
            EPOLLMSG = (int)(1U << 10), /* Never reported. */
            EPOLLRDHUP = (int)(1U << 13),
            EPOLLONESHOT = (int)(1U << 31);

struct F_API FEpollEvent {
  uint32 events;
  FEpollData data;
};

enum class EPollOp { Add = 1, Mod = 2, Del = 3};

F_API EpollHandle EPollCreate(int size);
//under win32, only support Level Triggered
F_API EpollHandle EPollCreate1(int flags);
F_API int EPollCtl(EpollHandle ephnd, EPollOp op, Socket sock,
                    FEpollEvent *event);
F_API int EPollClose(EpollHandle ephnd);
F_API int EPollWait(EpollHandle ephnd, FEpollEvent *events, int maxevents,
                     int timeout);

} // namespace Fei
#endif