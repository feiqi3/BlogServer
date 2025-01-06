#ifndef FTCPCONNECTION_H
#define FTCPCONNECTION_H

#include <memory>

#include "FDef.h"
#include "FNoCopyable.h"

namespace Fei{
    class FEventLoop;
    class FEvent;
    class FSock;
    class FBuffer;
    
    class FTcpConnection : public FNoCopyable,public std::enable_shared_from_this<FTcpConnection>{
        public:
        
        //High water callback: when there are too much data in inBuffer/outBuffer
        //                     While user consume it slow, then this will be called
        //Low  water callback: when data in buffer reach a user set limit, call this. 

        FTcpConnection(FEventLoop* loop, Socket s, FSocketAddr addrIn);
        ~FTcpConnection();

        void send(const char* data,uint64 len);

        private:

        //When output buffer is empty, send directly,
        //else queued in loop and send by buffer.
        void sendInLoop(const char* data,uint64 len);
        void handleRead();
        void handleWrite();
        void handleClose();
        void handleError();
        void handleWriteComplete();

        FEventLoop * m_loop;
        std::unique_ptr<FSock> m_sock;
        

        FSocketAddr m_addrIn;
        std::shared_ptr<FEvent> m_event;

        std::unique_ptr<FBuffer> inBuffer;
        std::unique_ptr<FBuffer> outBuffer;

    };

}

#endif