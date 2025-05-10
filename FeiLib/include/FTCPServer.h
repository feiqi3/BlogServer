#ifndef FFTcpServer_H
#define FFTcpServer_H
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
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
    //Not thread safe, will cause running serve GG
    static void initGlobalSSLEnv(const std::string& certificateFile,const std::string& privateKeyFile);
    static void deinitGlobalSSLEnv();
    void run();
    void stop(bool forceClose = false);

    void addListenPort(uint32 port,bool reuseport =false);
    void addSslListenPort(uint32 port,bool reuseport = false);
    void removeListenPort(uint32 port);
    void setOnConnEstablisedCallback(TcpConnectionEstablishedCallback cb){mOnEstablishedCallback = std::move(cb);}
    void setOnMessageCallback(TcpMessageCallback cb){mOnMessageCallback = std::move(cb);}
    void setOnCloseCallback(TcpCloseCallback cb){mOnCloseCallback = std::move(cb);}
    void setOnWriteCompleteCallback(TcpWriteCompleteCallback cb){mWriteCompleteCallback = std::move(cb);}
    void setOnIdleCallback(TcpIdleCallback cb){mOnIdleCallback = std::move(cb);}
    
    //Tick by user in there app
    TickEventId addTickEvent(AppTickEvent event);
    void removeEvent(TickEventId);
    void tickUserEvent();
    private:
    
    void onNewConnIn(Socket inSock,FSocketAddr addr,FSocketAddr addrAccept);
    void onClose(FTcpConnPtr ptr);
    TcpConnectionEstablishedCallback mOnEstablishedCallback;
    TcpMessageCallback mOnMessageCallback;
    TcpCloseCallback mOnCloseCallback;
    TcpIdleCallback mOnIdleCallback;
    TcpWriteCompleteCallback mWriteCompleteCallback;
    std::unique_ptr<FEventLoop> m_listenerLoop;
    std::vector<std::unique_ptr<FEventLoop>> m_subLoops;
    uint32 m_threadNums;
    std::vector<std::unique_ptr<FAcceptor>> m_acceptors;
    std::vector<uint32> m_sslPort;
    std::map<Socket, FTcpConnPtr> m_tcpConns;
    std::map<TickEventId, AppTickEvent> m_tickEvents;
    std::atomic_int m_tickEventId = 0;
    uint32 IOThread_Chooser = 0;
    bool m_running = false;
    //second
    int mSocketKeepAlive = -1;
    int mTcpConnIdleTime = -1;
    std::mutex m_tickEventMutex;
    std::mutex m_mutex;
};
}

#endif