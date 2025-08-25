#ifndef FHTTPSERVER_H
#define FHTTPSERVER_H
#include <memory>
#include <string>
#include "FException.h"
#include "FCallBackDef.h"

#include "FDef.h"
#include "Http/FHttpRequestParser.h"
#include "FHttp2Helper.h"
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
	static void initSSLenv(const std::string& certFile,const std::string& privateKey);
	static void deinitSSLenv();
	static void loadPushPromiseList(const std::string& path);
	void addListenPort(uint32 port);
	void addSSLPort(uint32 port);
	void removeListenPort(uint32 port);
	void run(); 
	void stop(bool force = false);
	void setOnInternalErrorCB(InternalErrorCallback cb) { mInternalErrCallback = std::move(cb); }
	void setOnRoutNotMatchCB(RouteNotMatchCallback cb) { mRouteNotMatchCallback = std::move(cb); }
	void setPreSendResponseCB(PreSendHttpResponseCallback cb) { mPreSendCallback = std::move(cb); }
	void setConnFilterCB(ConnectionFilterCallback cb) { mConnFilterFunc = std::move(cb); }
	TickEventId addAppTickEvent(AppTickEvent event);
	void removeAppTickEvent(TickEventId id);
	void tickAppEvents();
public:
	static bool getContentTypeByPath(const std::string& path,std::string& extensionName);

	struct HttpConnData{
		FHttpParser parser;
		bool shouldKeepAlive = true;
		uint32 hasSurvivedTime = 0;
		std::unique_ptr<FHttp2Context> http2Ctx = nullptr;
	};

private:
	void handleTcpIn(const FTcpConnPtr& ptr, FBufferReader& reader);
	void handleRequestSend(const FTcpConnPtr& ptr, const FHttpRequest& request, FHttpResponse& response);
	void preProcessHttpRequestSend(const FTcpConnPtr& ptr,const FHttpRequest& request, FHttpResponse& response);
	void handleTcpConnEstablish(const FTcpConnPtr& ptr);
	void handleTcpConnClosed(const FTcpConnPtr& ptr);
	void preProcessTcpConn(const FTcpConnPtr& ptr,const FHttpRequest& request);
	void handleTcpIdle(const FTcpConnPtr& ptr);
	void handleTcpWriteComplete(const FTcpConnPtr& ptr);
	private:
	void defaultHandleRouterMismatchFunc(const FHttpRequest& request, FHttpResponse& response);
	void defaultExceptionFunc(const FHttpRequest& request, FHttpResponse& response,const ::Fei::FException& exception);
	void http1Process(const FTcpConnPtr& ptr, FBufferReader& reader);
	void http2Process(const FTcpConnPtr& ptr, FBufferReader& reader);
	FHttpResponse httpHandle(const FTcpConnPtr& ptr,const FHttpRequest& request);
private:
	std::unique_ptr<FTcpServer> mTcpServer;
	PreSendHttpResponseCallback mPreSendCallback;
	InternalErrorCallback mInternalErrCallback;
	RouteNotMatchCallback mRouteNotMatchCallback;
	ConnectionFilterCallback mConnFilterFunc;
	TickEventId mRouteCacheCleanEventId = 0;
	

	// In Seconds

	//Time wait for uncomplete http request
	//if the time is out, the connection will be closed.
	int mHttpRequestWaitTime = -1;

	//Time wait for idle connection
	//if the time is out, the connection will be closed.
	int mHttpConnectionTimeout = -1;
	int mHttp2DrainTimeOut = 15;//seconds
	bool mHttp2CloseImm = false;
};
} // namespace Fei

#endif