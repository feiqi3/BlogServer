#ifndef FFTcpServer_H
#define FFTcpServer_H
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "FDef.h"
#include "FCallBackDef.h"
#include "FNoCopyable.h"


namespace Fei {
class FEventLoop;
class FAcceptor;
class FSock;
class F_API FTcpServer : public FNoCopyable{
    public:
    FTcpServer(uint32 threadNum);
    ~FTcpServer();
    void init();
    void run();
    void stop(bool forceClose = false);

    void addListenPort(uint32 port,bool reuseport =false);
    void removeListenPort(uint32 port);
    void setOnConnEstablisedCallback(TcpConnectionEstablishedCallback cb){mOnEstablishedCallback = std::move(cb);}
    void setOnMessageCallback(TcpMessageCallback cb){mOnMessageCallback = std::move(cb);}
    void setOnCloseCallback(TcpCloseCallback cb){mOnCloseCallback = std::move(cb);}
    void setOnWriteCompleteCallback(TcpWriteCompleteCallback cb){mWriteCompleteCallback = std::move(cb);}
    private:
    
    void onNewConnIn(Socket inSock,FSocketAddr addr);
    void onClose(FTcpConnPtr ptr);
    TcpConnectionEstablishedCallback mOnEstablishedCallback;
    TcpMessageCallback mOnMessageCallback;
    TcpCloseCallback mOnCloseCallback;
    TcpWriteCompleteCallback mWriteCompleteCallback;
    std::unique_ptr<FEventLoop> m_listenerLoop;
    std::vector<std::unique_ptr<FEventLoop>> m_subLoops;
    uint32 m_threadNums;
    std::vector<std::unique_ptr<FAcceptor>> m_acceptors;
    std::map<Socket, FTcpConnPtr> m_tcpConns;
    uint32 IOThread_Chooser = 0;
    bool m_running = false;

    std::mutex m_mutex;
};
}

#endif