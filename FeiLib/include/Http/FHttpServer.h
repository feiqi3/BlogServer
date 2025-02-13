#ifndef FHTTPSERVER_H
#define FHTTPSERVER_H
#include <memory>
#include "FException.h"
#include "FCallBackDef.h"

#include "FDef.h"
namespace Fei{
	class FTcpServer;
}
namespace Fei::Http {

class FHttpRequest;
class FHttpResponse;

using PreSendHttpResponseCallback = std::function<void(const FHttpRequest&, FHttpResponse&)>;
using InternalErrorCallback = std::function<void(const FHttpRequest&, FHttpResponse&, const ::Fei::FException&)>;
using RouteNotMatchCallback = std::function<void(const FHttpRequest&, FHttpResponse&)>;
//if true, the connection will not be processed by controller's function.
using ConnectionFilterCallback = std::function<bool(const FHttpRequest&, FHttpResponse&)>;

class F_API FHttpServer {
public:
	FHttpServer(uint32 threadNums);
	~FHttpServer();
	void addListenPort(uint32 port);
	void removeListenPort(uint32 port);
	void run(); 
	void stop(bool force = false);
	void setOnInternalErrorCB(InternalErrorCallback cb) { mInternalErrCallback = std::move(cb); }
	void setOnRoutNotMatchCB(RouteNotMatchCallback cb) { mRouteNotMatchCallback = std::move(cb); }
	void setPreSendResponseCB(PreSendHttpResponseCallback cb) { mPreSendCallback = std::move(cb); }
	void setConnFilterCB(ConnectionFilterCallback cb) { mConnFilterFunc = std::move(cb); }
public:
	static bool getContentTypeByPath(const std::string& path,std::string& extensionName);

private:
	void handleTcpIn(const FTcpConnPtr& ptr, FBufferReader& reader);
	void handleRequestSend(const FTcpConnPtr& ptr, const FHttpRequest& request, FHttpResponse& response);
	void preProcessHttpRequestSend(const FTcpConnPtr& ptr,const FHttpRequest& request, FHttpResponse& response);
	void handleTcpConnEstablish(const FTcpConnPtr& ptr);
	void handleTcpConnClosed(const FTcpConnPtr& ptr);
	void preProcessTcpConn(const FTcpConnPtr& ptr,const FHttpRequest& request);
private:
	void defaultHandleRouterMismatchFunc(const FHttpRequest& request, FHttpResponse& response);
	void defaultExceptionFunc(const FHttpRequest& request, FHttpResponse& response,const ::Fei::FException& exception);

private:
	std::unique_ptr<FTcpServer> mTcpServer;
	PreSendHttpResponseCallback mPreSendCallback;
	InternalErrorCallback mInternalErrCallback;
	RouteNotMatchCallback mRouteNotMatchCallback;
	ConnectionFilterCallback mConnFilterFunc;
};
} // namespace Fei

#endif