#pragma once
#include <functional>
#include <memory>

namespace Fei {

class FTcpConnection;
using FTcpConnPtr = std::shared_ptr<FTcpConnection>;

class FBuffer;

typedef std::function<void()> EventCallback;
typedef std::function<void()> ReadEventCallback;
typedef std::function<void()> WriteEventCallback;

typedef std::function<void(FTcpConnPtr,FBuffer&)> TcpMessageCallback;
typedef std::function<void(FTcpConnPtr)> TcpWriteCompleteCallback;
typedef std::function<void(FTcpConnPtr)> TcpConnectionEstablishedCallback;
typedef std::function<void(FTcpConnPtr)> TcpCloseCallback;
} // namespace Fei