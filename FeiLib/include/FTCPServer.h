#ifndef FTCPSERVER_H
#define FTCPSERVER_H
#include <memory>
#include <utility>
#include <vector>

#include "FDef.h"
#include "FCallBackDef.h"


namespace Fei {
class FEventLoop;
class FAcceptor;
class FSock;
class TcpServer{
    public:
    TcpServer(uint32 threadNum);
    void init();
    void run();
    void stop(bool forceClose = false);

    void addListenPort(uint32 port);
    void removeListenPort(uint32 port);
    void setOnConnEstablisedCallback(TcpConnectionEstablishedCallback cb){mOnEstablishedCallback = std::move(cb);}
    void setOnMessageCallback(TcpMessageCallback cb){mOnMessageCallback = std::move(cb);}
    void setOnCloseCallback(TcpCloseCallback cb){mOnCloseCallback = std::move(cb);}

    private:

    void onNewConnIn(Socket inSock,FSocketAddr addr);

    TcpConnectionEstablishedCallback mOnEstablishedCallback;
    TcpMessageCallback mOnMessageCallback;
    TcpCloseCallback mOnCloseCallback;
    std::unique_ptr<FEventLoop> m_listenerLoop;
    std::vector<std::unique_ptr<FEventLoop>> m_subLoops;
    uint32 m_threadNums;
    std::vector<std::unique_ptr<FAcceptor>> m_acceptors;

    uint32 IOThread_Chooser = 0;
};
}

#endif