#include "FSocket.h"
#include "FeiLib/FSocket.h"
#include "cstdio"
#include <cstdio>
int main(){
    using namespace Fei;
    Socket socket;
    FeiInit();
    SocketStatus status = Create(socket);
    if(status != SocketStatus::Success){
        printf("Create socket failed: %s\n", StatusToStr(status));
        return -1;
    }
    status = Bind(socket, "0.0.0.0", 12345);
    if (status != SocketStatus::Success) {
        printf("Bind failed: %s\n", StatusToStr(status));
        printf("Error: %s\n", GetErrorStr().c_str());
        return -1;
    }
    status = Listen(socket, 5);
    if (status != SocketStatus::Success) {
        printf("Listen failed: %s\n", StatusToStr(status));
        return -1;
    }
    while (1) {
        FSocketAddr addr;
        char data[128] = {};
        Socket client;
        status = Accept(socket,client, &addr);
        if (status != SocketStatus::Success) {
            printf("Accept failed: %s\n", StatusToStr(status));
            printf("Error: %s\n", GetErrorStr().c_str());
            break;
        }
        int recLen = 0;
        status = Recv(client,data, 127,RecvFlag::None , recLen);
        printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3,recLen,data);
        Close(client);
        if (status != SocketStatus::Success) {
            printf("Close client failed: %s\n", StatusToStr(status));
            printf("Error: %s\n", GetErrorStr().c_str());
            break;
        }
    }
    Close(socket);
    FeiUnInit();
    return 0;
}