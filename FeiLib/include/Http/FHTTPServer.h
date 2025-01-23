#ifndef FHTTPSERVER_H
#define FHTTPSERVER_H
#include <memory>
#include "FCallBackDef.h"

#include "FDef.h"
namespace Fei{
	class FTcpServer;
}
namespace Fei::Http {

class FHttpRequest;
class FHttpResponse;

using PreSendHttpResponseCallback = std::function<void(const FTcpConnPtr&, const FHttpRequest&, FHttpResponse&)>;
using InternalErrorCallback = std::function<void(const FTcpConnPtr&, const FHttpRequest&, FHttpResponse&)>;



class FHttpServer {
public:
	FHttpServer(uint32 threadNums);
	void run(); 
	void stop();
	void setOnInternalErrorCB(InternalErrorCallback cb) { mInternalErrCallback = std::move(cb); }
	void setPreSendResponseCB(PreSendHttpResponseCallback cb) { mPreSendCallback = std::move(cb); }
public:
	static bool getContentTypeByPath(const std::string& path,std::string& extensionName);

private:
	void handleTcpIn(const FTcpConnPtr& ptr, FBufferReader& reader);
	void handleRequestSend(const FTcpConnPtr& ptr, FHttpResponse& response);
	void preProcessHttpRequestSend(const FTcpConnPtr& ptr,const FHttpRequest& request, FHttpResponse& response);
	void handleTcpConnEstablish(const FTcpConnPtr& ptr);
	void handleRequestParseError(const FTcpConnPtr& ptr);
	void handlePathMismatchError(const FTcpConnPtr& ptr);
	void preProcessTcpConn(const FTcpConnPtr& ptr,const FHttpRequest& request);


private:
	std::unique_ptr<FTcpServer> mTcpServer;
	PreSendHttpResponseCallback mPreSendCallback;
	InternalErrorCallback mInternalErrCallback;
};
} // namespace Fei

#endif