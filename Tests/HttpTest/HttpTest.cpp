#include "FCallBackDef.h"
#include "FDef.h"
#include "FEvent.h"
#include "FeiLibIniter.h"
#include "FSocket.h"
#include "FTCPConnection.h"
#include "FTcpServer.h"
#include "FREventDef.h"

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
#include "FeiLib/FSocket.h"

void FTcpServer_echo();
void HttpRead();
void HttpEpollRead();

int main() {
    FTcpServer_echo();
}

void FTcpServer_echo() {
    using namespace Fei;
    FeiLibInit();
    FTcpServer* server = new FTcpServer(1);
    server->init();
    server->addListenPort(80);
    server->setOnConnEstablisedCallback([](FTcpConnPtr ptr) {
        auto addr = ptr->getAddr();
    std::cout << "use count " << ptr.use_count() << "\n";
    printf("Accept client: %d.%d.%d.%d : %d\n", addr.un.un_byte.a0,
        addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3,addr.port);
    ptr->setReading(true);
        });

    server->setOnMessageCallback([](FTcpConnPtr ptr, FBufferReader& buf) {
        auto len = buf.readTo(0, 0);
    std::vector<char> mBytes(len + 1);
    len = buf.readTo(mBytes.data(), len);
    auto addr = ptr->getAddr();
    std::cout << "use count " << ptr.use_count() << "\n";
    printf("Accept client: %d.%d.%d.%d, data len: %d, data: %s\n",
        addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2,
        addr.un.un_byte.a3, len, mBytes.data());
        });

    server->setOnCloseCallback([](FTcpConnPtr ptr) {
        (void)ptr;
    std::cout << "use count " << ptr.use_count() << "\n";
    auto addr = ptr->getAddr();
    printf("Close client: %d.%d.%d.%d\n", addr.un.un_byte.a0,
        addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3);
        });
    server->run();
    while (1) {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    }
    FeiLibUnInit();
}

void HttpRead()
{
    using namespace Fei;
    FeiLibInit();
    Socket s,c;
    Create(s);
    Bind(s, inAddrAny, 80);
    Listen(s, 1000);
    char data[1024] = {};
    int len = 0;
    FSocketAddr add;
    Accept(s, c, &add);
    while (1) {
        std::memset(data, 0, 1024);
        Recv(c, data, 1024, RecvFlag::None, len);
        if (len == 0)break;
        std::cout << data << "\n";
    }

}

void HttpEpollRead()
{
    using namespace Fei;
    FeiLibInit();

    auto handle = EPollCreate1(0);

    Socket s;
    Create(s);
    Bind(s, inAddrAny, 80);
    Listen(s, 1000);
    Socket c;
    FSocketAddr out;
    Accept(s, c, &out);
    FEpollEvent event{ .events =REvent::ToEpoll(REvent::In | REvent::Pri)};
    event.data.sock = c;
    EPollCtl(handle, EPollOp::Add, c, &event);
    std::cout << GetErrorStr();
    while (1) {
        FEpollEvent outEvent{};
        int num = EPollWait(handle, &outEvent, 1, 10);
        if (num > 0) {
            auto c = outEvent.data.sock;
            char data[1024] = {};
            int recLen = 0;
            Recv(c, data, 1024, RecvFlag::None, recLen);
            if (recLen <= 0) {
                std::cout << GetErrorStr();
                return;
            }
            std::cout << data<<"\n";
        }
        else {
            return;
        }
    }

    FeiLibUnInit();
}
