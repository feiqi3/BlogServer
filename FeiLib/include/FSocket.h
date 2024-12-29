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

enum class RecvFlag { None = 0, Peek = 1, WaitAll = 2,DontWait = 3, MAX_FLAG = DontWait };

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
F_API SocketStatus Accept(Socket listen, Socket& client, FSocketAddr *addr);
F_API SocketStatus Send(Socket socket, const char *data, int len);
F_API SocketStatus Recv(Socket socket, char *data, int len, RecvFlag flag,
                        int &recv_len);
F_API std::string GetErrorStr();

inline const char *inAddrAny = "0.0.0.0";
inline const char *inAddrLoopback = "127.0.0.1";
inline const char *inAddrBroadcast = "255.255.255.255";

} // namespace Fei
#endif