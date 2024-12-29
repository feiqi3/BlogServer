#include "FSocket.h"
#include "FeiLib/FSocket.h"
#include "cstdio"
int main() {
  using namespace Fei;
  FeiInit();
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
  status = Send(socket, data, 13);
  if (status != SocketStatus::Success) {
    printf("Send failed: %s\n", StatusToStr(status));
    printf("Error: %s\n", GetErrorStr().c_str());
    return -1;
  }
  Close(socket);
  FeiUnInit();
}