#ifndef FSOCKET_H
#define FSOCKET_H
#include "FDef.h"
#include <string>

namespace Fei {


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

struct F_API FPollfd {
  Socket fd;
  short events;
  short revents;
};

namespace SocketOpts{
  enum{
    Stream = 1 << 0,
    NoBlock = 1 << 1,
  };
}

namespace SocketType{
  enum{
    Stream,
    Dgram,
  };
};

namespace SocketProt{
  enum{
    TCP,
    UDP,
  };
};

F_API SocketStatus FeiInit();
F_API SocketStatus FeiUnInit();

F_API SocketStatus Create(Socket &socket);
F_API SocketStatus Create(Socket &socket,int type,int protocal);
F_API SocketStatus Close(Socket socket);
F_API SocketStatus Connect(Socket socket, const char *ip, uint16 port);
F_API SocketStatus Connect(Socket socket, FSocketAddr addr);
F_API SocketStatus Bind(Socket socket, const char *ip, uint16 port);
F_API SocketStatus Bind(Socket socket, FSocketAddr addr);
F_API SocketStatus Listen(Socket socket, int backlog);
F_API SocketStatus Accept(Socket listen, Socket &client, FSocketAddr *addr);
F_API SocketStatus Send(Socket socket, const char *data, int len,int& writeLen);
F_API SocketStatus Recv(Socket socket, char *data, int len, RecvFlag flag,
                        int &recv_len);


F_API void ShutDown(Socket socket,bool shutWr,bool shutRd);

 struct iovec{
     void *iov_base; /* Pointer to data. */
     size_t iov_len; /* Length of data. */
};

F_API long SendV(Socket socket, iovec* vec, int count);
F_API int Readv(Socket handle, struct iovec *iov, int count);
enum class SockOpt{
  ReusePort,
  ReuseAddr,
  KeepAlive,
  KeepIntvl,
  KeepIdle,
  NoneBlock,
  CloseOnExec,
  NoneBlockAndCloseOnExec,
};
F_API int SetSockOpt(Socket s,SockOpt opt,bool on);
F_API int SetSockOpt(Socket s,SockOpt opt, int v);

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

//Linux Specific, means the other side of the connection has closed its write, but still can read
//In windows we just ignore this flag
#elif defined (__linux__)
const short POLLRDNORM = 0x040;
const short POLLRDBAND = 0x080;
const short POLLIN = 0x001;
const short POLLPRI = 0x002;

const short POLLWRNORM = 0x100;
const short POLLOUT = 0x004;
const short POLLWRBAND = 0x200;

const short POLLERR = 0x008;
const short POLLHUP = 0x010;
const short POLLNVAL = 0x020;
const short POLLRDHUP	= 0x2000;
#endif

#ifdef _WIN32
const int   EPOLLIN = (int)(1U << 0),
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
#elif defined (__linux__)
const int
    EPOLLIN = 0x001,
    EPOLLPRI = 0x002,
    EPOLLOUT = 0x004,
    EPOLLRDNORM = 0x040,
    EPOLLRDBAND = 0x080,
    EPOLLWRNORM = 0x100,
    EPOLLWRBAND = 0x200,
    EPOLLMSG = 0x400,
    EPOLLERR = 0x008,
    EPOLLHUP = 0x010,
    EPOLLRDHUP = 0x2000,
    EPOLLONESHOT = 1u << 30;
#endif


enum class EPollOp { 
  Add
  #ifdef _WIN32
   = 1 
  #else
   = 1
  #endif
  ,
  Mod 
    #ifdef _WIN32
   = 2 
  #else
   = 3
  #endif
  , 
  Del 
    #ifdef _WIN32
   = 3 
  #else
   = 2
  #endif
  ,
  };
F_API bool isEpollHandleValid(EpollHandle handle);
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