#include "FSocket.h"
#include "FeiLibIniter.h"
#include "cstdio"
int main() {
  using namespace Fei;
  FeiLibInit();
  Socket socket;
  SocketStatus status = Create(socket);
  if (status != SocketStatus::Success) {
    printf("Create socket failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return -1;
  }
  status = Connect(socket, "127.0.0.1", 12345);
  if (status != SocketStatus::Success) {
    printf("Connect failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return -1;
  }
  char data[128] = "Hello, world!";
  int realSendLen = 0;
  status = Send(socket, data, 13,realSendLen);
  if (status != SocketStatus::Success || realSendLen < 13) {
    printf("Send failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return -1;
  }
  char recvBuffer[1024] = {};
  int realRecvLen = 0;
  status = Recv(socket, recvBuffer,1024,RecvFlag::None,realRecvLen);
  if (status != SocketStatus::Success ) {
      printf("Send failed: %s\n", StatusToStr(status));
      printf("Error: %s\n", GetErrorStr().c_str());
      return -1;
  }
  std::printf("Receive from server: %s\n", recvBuffer);
  Close(socket);
  FeiLibUnInit();
}