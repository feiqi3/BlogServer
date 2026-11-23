#include <functional>
#include <sstream>
#include "Http/FHttpServer.h"
#include "FConfigReader.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpRequestParser.h"
#include "Http/FHttpResponse.h"
#include "FTCPServer.h"
#include "FTCPConnection.h"
#include "Http/FRouter.h"
#include "FLogger.h"
#include "Http/FHttp2Helper.h"
#include <chrono>
#include <algorithm>
#include <future>
#include <stdexcept>
#include "FEventLoop.h"
#include "FBuffer.h"
#include "FDeferredResponse.h"
#include <mutex>
#include <unordered_map>
#include <set>
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include "FEventLoop.h"
#endif
#define MODULE_NAME "HttpServer"
#define ERROR_ROUTE_PATH "/error"
#define DEFAULT_CONTENT_TYPE "text/html"
#define DEFAULT_CHAT_SET "; charset=UTF-8"
#define DEFAULT_HTTP_VERSION Version::Http11

static const std::string DefaultCharSet = DEFAULT_CHAT_SET;
static const std::string DefaultContentType = DEFAULT_CONTENT_TYPE + DefaultCharSet;
static const std::string DefaultServerName = "by FeiLib";
extern const std::unordered_map<std::string, std::string> extensionToContentType;

namespace {

	Fei::Http::FHttpServer::HttpConnData* getDataFromTcpConn(const Fei::FTcpConnPtr& ptr) {
		auto data = static_cast<Fei::Http::FHttpServer::HttpConnData*>(ptr->getUserData());
		if (data == nullptr) {
			data = new Fei::Http::FHttpServer::HttpConnData();
			data->hasSurvivedTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			ptr->setUserData(data);
		}

		if (ptr->isHttp2() && data->http2Ctx == nullptr) {
			data->http2Ctx = (std::unique_ptr<Fei::Http::FHttp2Context>(new Fei::Http::FHttp2Context(ptr->getAddr())));
		}

		return data;
	}

	void DestroyDataFromTcpConn(const Fei::FTcpConnPtr& ptr) {
		auto data = static_cast<Fei::Http::FHttpServer::HttpConnData*>(ptr->getUserData());
		if (data != nullptr) {
			delete data;
			ptr->setUserData(nullptr);
		}
	}

	bool _getFileExtension(const std::string& filename, std::string& extension) {
		static std::string fileExtensionSeperator = ".";
		auto dotPos = std::find_first_of(filename.rbegin(), filename.rend(), fileExtensionSeperator.begin(), fileExtensionSeperator.end());
		if (dotPos != filename.rend()) {
			auto dotPosIdx = filename.rend() - dotPos;
			extension = std::string(filename.begin() + dotPosIdx, filename.end());
			return true;
		}
		else {
			extension = DefaultContentType;
			return false;
		}
	}

bool _getContentTypeByPath(const std::string& path, std::string& extensionName)
{
	_getFileExtension(path, extensionName);
	auto itor = extensionToContentType.find(extensionName);
	if (itor == extensionToContentType.end())return false;
	extensionName = itor->second;
	return true;

}
}

namespace Fei::Http {
    struct DeferredConnectionState {
        std::unordered_map<uint32_t,FHttpResponder> pending;
        std::shared_ptr<FBuffer> buffered;
    };
    struct DeferredRegistry {
        struct Entry { FEventLoop* loop; FHttpResponder responder; std::weak_ptr<FTcpConnection> connection; };
        std::mutex mutex;
        bool accepting=true;
        std::unordered_map<uint64_t,Entry> active;
        std::size_t limit=256;
        uint64_t next=0;
    };

	void FHttpServer::initSSLenv(const std::string& certFile,const std::string& privateKey){
		FTcpServer::initGlobalSSLEnv(certFile, privateKey);
	}

	void FHttpServer::deinitSSLenv(){
		FTcpServer::deinitGlobalSSLEnv();		
	}

	void FHttpServer::loadPushPromiseList(const std::string& path){
		FHttp2Context::loadPushPromiseData(path);
	}

	FHttpServer::FHttpServer(uint32 threadNums) : mTcpServer(std::make_unique<FTcpServer>(threadNums))
	{
        m_deferred=std::make_shared<DeferredRegistry>();
        if(auto limit=FConfigReader::instance()->getCfg("HttpMaxDeferredRequests")){
            auto value=std::stoull(*limit);
            if(value==0||value>65536)throw std::invalid_argument("Invalid HttpMaxDeferredRequests");
            m_deferred->limit=value;
        }
		if (!FRouter::valid()) {
			new FRouter;
		}
		mTcpServer->init();
		mTcpServer->setOnConnEstablisedCallback(std::bind(&FHttpServer::handleTcpConnEstablish, this,std::placeholders::_1));
		mTcpServer->setOnMessageCallback(std::bind(&FHttpServer::handleTcpIn, this, std::placeholders::_1, std::placeholders::_2));
		mTcpServer->setOnCloseCallback(std::bind( & FHttpServer::handleTcpConnClosed,this,std::placeholders::_1 ));
		mTcpServer->setOnIdleCallback(std::bind(&FHttpServer::handleTcpIdle, this, std::placeholders::_1));
		mTcpServer->setOnWriteCompleteCallback(std::bind(&FHttpServer::handleTcpWriteComplete, this, std::placeholders::_1));
		mRouteCacheCleanEventId = mTcpServer->addTickEvent(std::bind(&FRouter::checkRouteCache,FRouter::instance(),std::placeholders::_1));
		FRouter::instance()->lateInit();

		/*
			HttpRequestWaitTime: the max wait time for a http request's header to be completely received. In seconds

			HttpConnectTimeout: the max wait time for a http connection to be idle. In seconds


		*/
		const auto cfg = FConfigReader::instance();
		{
			auto headerWait = cfg->getCfg("HttpRequestWaitTime");
			if(headerWait.has_value()){
				FCfgUtils::toNumber(headerWait.value(), this->mHttpRequestWaitTime);
			}
			auto httpConnectTimeout = cfg->getCfg("HttpConnectTimeout");
			if(httpConnectTimeout.has_value()){
				FCfgUtils::toNumber(httpConnectTimeout.value(), this->mHttpConnectionTimeout);
			}
			auto http2CloseImm = cfg->getCfg("Http2CloseImm");
			if(http2CloseImm.has_value()){
				int val = 0;
				FCfgUtils::toNumber(http2CloseImm.value(), val);
				if(val != 0){
					this->mHttp2CloseImm = true;
				}
			}
			auto http2DrainTimeout = cfg->getCfg("Http2DrainTimeout");
			if(http2DrainTimeout.has_value()){
				FCfgUtils::toNumber(http2DrainTimeout.value(), this->mHttp2DrainTimeOut);
				this->mHttp2DrainTimeOut = std::max(0, this->mHttp2DrainTimeOut);
			}

		}
		FHttp2Context::loadConfig();
	}

	FHttpServer::~FHttpServer() {
        if (m_running) stop();
		mTcpServer->removeEvent(mRouteCacheCleanEventId);
		if (FRouter::valid()) {
			delete FRouter::instance();
		}
	}

	void FHttpServer::addListenPort(uint32 port)
	{
		this->mTcpServer->addListenPort(port,true);
	}

	void FHttpServer::addSSLPort(uint32 port){
		this->mTcpServer->addSslListenPort(port,true);
	}

	void FHttpServer::removeListenPort(uint32 port)
	{
		this->mTcpServer->removeListenPort(port);
	}

	void FHttpServer::run()
	{
		mTcpServer->run();
        m_running = true;
#ifdef FEILIB_ENABLE_SCRIPT
        if (m_scriptEngine) m_scriptEngine->start();
#endif
	}

	void FHttpServer::stop(bool force)
	{
        if(m_running)drainDeferred();
#ifdef FEILIB_ENABLE_SCRIPT
        if (m_running && m_scriptEngine) {
            if (FEventLoop::getCurrentLoop()) throw std::logic_error("Stop the server from its control thread");
            std::promise<void> drained; auto done=drained.get_future();
            m_scriptEngine->shutdown([&drained]{drained.set_value();});
            done.get();
        }
#endif
		mTcpServer->stop(force);
        m_running = false;
	}
    FScriptEngine& FHttpServer::scriptEngine() {
#ifdef FEILIB_ENABLE_SCRIPT
        if(m_scriptEngine)return *m_scriptEngine;
        return scriptEngine(FScriptOptions{});
#else
        throw std::logic_error("FeiLib was built without scripting");
#endif
    }
    FScriptEngine& FHttpServer::scriptEngine(const FScriptOptions& options) {
#ifdef FEILIB_ENABLE_SCRIPT
        if(m_running||m_scriptEngine)throw std::logic_error("Configure the script engine once before run");
        m_scriptEngine=std::make_shared<FScriptEngine>(jobLoop(),options);
        return *m_scriptEngine;
#else
        (void)options;
        throw std::logic_error("FeiLib was built without scripting");
#endif
    }

	FEventLoop& FHttpServer::jobLoop() {
		return mTcpServer->jobLoop();
	}

	TickEventId FHttpServer::addAppTickEvent(AppTickEvent event){
		return mTcpServer->addTickEvent(event);
	}

	void FHttpServer::removeAppTickEvent(TickEventId id){
		mTcpServer->removeEvent(id);
	}

	void FHttpServer::tickAppEvents(){
		mTcpServer->tickUserEvent();
	}

	bool FHttpServer::getContentTypeByPath(const std::string& path, std::string& extensionName)
	{
		return _getContentTypeByPath(path, extensionName);
	}

	void FHttpServer::handleTcpIn(const FTcpConnPtr& ptr, FBufferReader& reader)
	{
		bool h2 = ptr->isHttp2();
		if (!h2) {
			http1Process(ptr, reader);
		}
		else {
			http2Process(ptr, reader);
		}
	}

	void FHttpServer::handleRequestSend(const FTcpConnPtr& ptr, const FHttpRequest& request, FHttpResponse& response)
	{
		preProcessHttpRequestSend(ptr, request, response);
	}

	void FHttpServer::preProcessHttpRequestSend(const FTcpConnPtr& ptr, const FHttpRequest& request, FHttpResponse& response)
	{
		if (mPreSendCallback) {
			mPreSendCallback(request, response);
		}
		bool hasBody = !response.getBody().empty();
		response.setHttpVersion(DEFAULT_HTTP_VERSION);
		{
			std::string serverName = "";
			response.getHeader("Server", serverName);
			serverName = serverName + DefaultServerName;
			response.addHeader("Server", serverName);
		}
		if (hasBody) {
			std::string hasContentType;
			if (!response.getHeader("Content-Type", hasContentType)) {
				std::string contentType;
				_getContentTypeByPath(request.getPath(), contentType);
				response.setContentType(contentType);
			}
		}
	}

	void FHttpServer::handleTcpConnEstablish(const FTcpConnPtr& ptr)
	{
		ptr->setReading(true);
	}

	void FHttpServer::handleTcpConnClosed(const FTcpConnPtr& ptr)
	{
        auto* data=static_cast<HttpConnData*>(ptr->getUserData());
        if(data&&data->deferred){
            auto pending=std::move(data->deferred->pending);
            for(auto& [id,responder]:pending)responder.cancel();
        }
		DestroyDataFromTcpConn(ptr);
	}
	void FHttpServer::handleTcpIdle(const FTcpConnPtr& ptr){
		//TODO: for http2, need to traversal streams.

		const auto connData = getDataFromTcpConn(ptr);

		auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		bool isTimeOut = false;
		if (!ptr->isHttp2()) {

			bool isHeadWaitTimeOut = false;
			bool isParseNotComplete = false;


			if (mHttpRequestWaitTime >= 0 && now - connData->hasSurvivedTime > mHttpRequestWaitTime) {
				isHeadWaitTimeOut = true;
			}

			if (mHttpConnectionTimeout >= 0 && now - connData->hasSurvivedTime > mHttpConnectionTimeout) {
				isTimeOut = true;
			}

			if (connData->parser.getState() != FHttpParser::EState::Done) {
				isParseNotComplete = true;
			}

			if (isHeadWaitTimeOut && isParseNotComplete) {
				Logger::instance()->log(MODULE_NAME, lvl::info, "request timeout from {}.{}.{}.{} : {} error.", ptr->getAddr().un.un_byte.a0, ptr->getAddr().un.un_byte.a1, ptr->getAddr().un.un_byte.a2, ptr->getAddr().un.un_byte.a3, ptr->getAddr().port);
				ptr->forceClose();
			}
			else if (isTimeOut) {
				Logger::instance()->log(MODULE_NAME, lvl::info, "connection timeout from {}.{}.{}.{} : {} error.", ptr->getAddr().un.un_byte.a0, ptr->getAddr().un.un_byte.a1, ptr->getAddr().un.un_byte.a2, ptr->getAddr().un.un_byte.a3, ptr->getAddr().port);
				ptr->forceClose();
			}
		}
		else {
			bool firstTimeOut = false;
			auto& h2 = connData->http2Ctx;

			if (mHttpRequestWaitTime >= 0 && now - connData->hasSurvivedTime > mHttpRequestWaitTime) {
				firstTimeOut = true;
			}

			if (mHttpConnectionTimeout >= 0 && now - connData->hasSurvivedTime > std::min(mHttpConnectionTimeout, mHttp2DrainTimeOut)) {
				isTimeOut = true;
			}

			if (firstTimeOut) {
				if(mHttp2CloseImm){
					ptr->forceClose();
				}

				if (isTimeOut) {
					Logger::instance()->log(MODULE_NAME, lvl::info, "connection timeout from {}.{}.{}.{} : {} , send goaway(h2).", ptr->getAddr().un.un_byte.a0, ptr->getAddr().un.un_byte.a1, ptr->getAddr().un.un_byte.a2, ptr->getAddr().un.un_byte.a3, ptr->getAddr().port);
					ptr->forceClose();
				}
				//send goaway, no more stream in
				if(!isTimeOut){
					h2->http2SubmitGoaway();
					h2->http2SendProcess(ptr);
				}
			}
		}

	}

	void FHttpServer::handleTcpWriteComplete(const FTcpConnPtr& ptr){
		auto connData = getDataFromTcpConn(ptr);
		if (!connData->shouldKeepAlive) {
			ptr->forceClose();
		}
	}

	void FHttpServer::preProcessTcpConn(const FTcpConnPtr& ptr, const FHttpRequest& request)
	{
		const auto connData = getDataFromTcpConn(ptr);
		auto addr = ptr->getAddr();
		Logger::instance()->log("FHttpServer", lvl::trace, "Http conntection established, from {}.{}.{}.{} : {}", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3, addr.port);
		if (!ptr->isHttp2())
		{
			std::string headerAttracted;
			bool hasConnectionIndicator = request.getHeader("Connection", headerAttracted);
			bool setKeepAlive = false;
			if (!hasConnectionIndicator) {
				if (request.getHttpVersion() == Version::Http11) {
					setKeepAlive = true;
				}
			}

			if (headerAttracted == "keep-alive") {
				setKeepAlive = true;
			}

			connData->shouldKeepAlive = setKeepAlive;
		}
	}

	void FHttpServer::defaultHandleRouterMismatchFunc(const FHttpRequest& request, FHttpResponse& response)
	{
		response.setStatusCode(StatusCode::_404);
		response.setBody("404: Page not found");
	}

	void FHttpServer::defaultExceptionFunc(const FHttpRequest& request, FHttpResponse& response,const FException& exception)
	{
		response.setStatusCode(StatusCode::_501);
		std::stringstream ss;
		ss << "500: Server Internal Error";
		for(auto&& str : exception.stackTrace()){
			ss<<"<br>"<<str;			
		}
		response.setBody(ss.str());
		return;
	}

	void FHttpServer::http1Process(const FTcpConnPtr& ptr, FBufferReader& reader)
	{
		auto http_data = getDataFromTcpConn(ptr);
        auto bufferRemaining=[&]{
            int size=0;auto bytes=reader.peekAll(size);
            if(!size)return;
            auto& deferred=*http_data->deferred;
            if(!deferred.buffered)deferred.buffered=std::make_shared<FBuffer>(1024);
            if(deferred.buffered->getReadableSize()+size>256*1024){ptr->forceClose();return;}
            deferred.buffered->Append(reinterpret_cast<const char*>(bytes),size);reader.expireSize(size);
        };
        if(http_data->deferred&&http_data->deferred->pending.contains(0)){bufferRemaining();return;}
        do {
		bool isParseDone = http_data->parser.parse(reader);

		if (!isParseDone) {
			if (http_data->parser.getState() == FHttpParser::EState::Error) {
				Logger::instance()->log(MODULE_NAME, lvl::info, "request error from {}.{}.{}.{} : {} error.", ptr->getAddr().un.un_byte.a0, ptr->getAddr().un.un_byte.a1, ptr->getAddr().un.un_byte.a2, ptr->getAddr().un.un_byte.a3, ptr->getAddr().port);
				ptr->forceClose();
			}
			return;
		}

		FHttpRequest request(http_data->parser);
		auto addr = ptr->getAddr();
		request.setAddrIn(addr);
		request.setAddrHost(ptr->getAddrAccept());
		auto response = httpHandle(ptr, request);
        if(response.isDeferred()){
            if(!beginDeferred(ptr,request,std::move(response)))return;
            if(ptr->getUserData())bufferRemaining();
            return;
        }
        const bool keepAlive=http_data->shouldKeepAlive;
        if(keepAlive)http_data->parser=FHttpParser();
        ptr->send(response.toString());
        if(!keepAlive)return;
        }while(reader.getBufferReadableSize()>0&&ptr->getState()!=FTcpConnection::TcpConnState::DisConnected);
	}

	void FHttpServer::http2Process(const FTcpConnPtr& ptr, FBufferReader& reader)
	{
		assert(ptr->isHttp2());

		//1. process data
		auto http_data = getDataFromTcpConn(ptr);
		auto& http2Ctx = http_data->http2Ctx;
		http2Ctx->http2RecvProcess(reader);
		http2Ctx->http2SendProcess(ptr);

		auto perStream = [&ptr,this,&http_data](const FHttpRequest& request,uint32_t streamID) {
			auto response = httpHandle(ptr, request);
            if(response.isDeferred()){beginDeferred(ptr,request,std::move(response),streamID);return true;}
			http_data->http2Ctx->http2SubmitResponseStream(streamID,response,true);
			if(http_data->http2Ctx->enablePush())
			{
				//Handle push promise   
				bool notMatchError = false;
				auto pushPromiseRequests = FHttp2Context::getPushPromise(request.getPath());
				for (auto& pushRequest : pushPromiseRequests) {
					FHttpResponse promiseResponse;
					auto routeResult = FRouter::instance()->route(request.getMethod(), request.getPath());
					if (!routeResult.isvalid()) {
						notMatchError = true;
					}
					if (notMatchError) {
						routeResult = FRouter::instance()->route(Method::GET, ERROR_ROUTE_PATH);
						if (!routeResult.isvalid()) {
							if (mRouteNotMatchCallback) {
								mRouteNotMatchCallback(request, promiseResponse);
							}
							else {
								defaultHandleRouterMismatchFunc(request, promiseResponse);
							}
						}
						else {
							promiseResponse = routeResult.controllerFunc(request, routeResult.pathVariable);
						}
					}else{
						promiseResponse = routeResult.controllerFunc(request, routeResult.pathVariable);
					}
					http_data->http2Ctx->http2SubmitPushPromise(streamID, pushRequest,promiseResponse);
				}
			}
			return true;
		};
		http2Ctx->traversalFinishedStreams(perStream);
		http2Ctx->http2SendProcess(ptr);
	}

	FHttpResponse FHttpServer::httpHandle(const FTcpConnPtr& ptr,const FHttpRequest& request)
	{

		preProcessTcpConn(ptr, request);
		auto addr = ptr->getAddr();
		FRouter::RouteResult routeResult;
		bool notMatchError = false;
		bool isFiltered = false;

		FHttpResponse response;
		if (mConnFilterFunc && mConnFilterFunc(request, response)) {
			isFiltered = true;
		}

		if (!request.isValid()) {
			Logger::instance()->log(MODULE_NAME, lvl::info, "request error from {}.{}.{}.{} : {} error.", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3, addr.port);
			notMatchError = true;
		}
		else if (!isFiltered) {
			routeResult = FRouter::instance()->route(request.getMethod(), request.getPath());
			if (!routeResult.isvalid()) {
				notMatchError = true;
			}
		}

		if (notMatchError) {
			routeResult = FRouter::instance()->route(Method::GET, ERROR_ROUTE_PATH);
			if (!routeResult.isvalid()) {
				if (mRouteNotMatchCallback) {
					mRouteNotMatchCallback(request, response);
				}
				else {
					defaultHandleRouterMismatchFunc(request, response);
				}
			}
			else {
				response = routeResult.controllerFunc(request, routeResult.pathVariable);
			}
		}
		else if (isFiltered) {

		}
		else {
			try
			{
				response = routeResult.controllerFunc(request, routeResult.pathVariable);
			}
			catch (::Fei::FException& exception)
			{
				Logger::instance()->log("FHttpServer", lvl::err, "Request handle error: request path: \"{}\", Reason: {}", request.getPath(), exception.what());
				if (mInternalErrCallback) {
					mInternalErrCallback(request, response, exception);
				}
				else {
					defaultExceptionFunc(request, response, exception);
				}
			}
			catch (std::exception& e) {
				Logger::instance()->log("FHttpServer", lvl::err, "Request handle error: request path: \"{}\", Reason: {}", request.getPath(), e.what());
				FException exception;
				defaultExceptionFunc(request, response, exception);
			}
		}

		if(!response.isDeferred())preProcessHttpRequestSend(ptr, request, response);
		return response;
	}

    bool FHttpServer::beginDeferred(const FTcpConnPtr& ptr,const FHttpRequest& request,FHttpResponse response,uint32_t streamId) {
        auto spec=std::move(response.mDeferred);
        auto* data=getDataFromTcpConn(ptr);
        if(!data->deferred)data->deferred=std::make_shared<DeferredConnectionState>();
        auto registry=m_deferred;
        std::weak_ptr<FTcpConnection> connection=ptr;
        uint64_t id;
        {std::lock_guard lock(registry->mutex);id=++registry->next;}
        auto responder=FHttpResponder::create(ptr->loop(),spec->timeout,
            [this,registry,connection,request,streamId,id](FHttpResponse completed)mutable{
                {std::lock_guard lock(registry->mutex);registry->active.erase(id);}
                auto ptr=connection.lock();
                if(!ptr||ptr->getState()==FTcpConnection::TcpConnState::DisConnected||!ptr->getUserData())return;
                auto* data=static_cast<HttpConnData*>(ptr->getUserData());
                if(!data->deferred||!data->deferred->pending.erase(streamId))return;
                preProcessHttpRequestSend(ptr,request,completed);
                if(streamId){
                    data->http2Ctx->http2SubmitResponseStream(streamId,completed,true);
                    data->http2Ctx->http2SendProcess(ptr);
                }else{
                    const bool keepAlive=data->shouldKeepAlive;
                    if(keepAlive)data->parser=FHttpParser();
                    ptr->send(completed.toString());
                    if(keepAlive&&ptr->getState()!=FTcpConnection::TcpConnState::DisConnected){
                        auto buffered=std::move(data->deferred->buffered);
                        if(buffered){FBufferReader reader(*buffered);http1Process(ptr,reader);}
                    }
                }
            });
        bool admitted,accepting;
        {std::lock_guard lock(registry->mutex);accepting=registry->accepting;admitted=accepting&&registry->active.size()<registry->limit;
         if(admitted)registry->active.emplace(id,DeferredRegistry::Entry{&ptr->loop(),responder,connection});}
        if(!admitted){
            responder.cancel();
            if(!accepting){ptr->forceClose();return false;}
            FHttpResponse busy;busy.setStatusCode(StatusCode::_429).setBody("Too many pending responses");
            preProcessHttpRequestSend(ptr,request,busy);
            if(streamId){data->http2Ctx->http2SubmitResponseStream(streamId,busy,true);data->http2Ctx->http2SendProcess(ptr);}
            else{data->shouldKeepAlive=false;busy.addHeader("Connection","close");ptr->send(busy.toString());}
            return false;
        }
        data->deferred->pending.emplace(streamId,responder);
        responder.onCancel([registry,id]{std::lock_guard lock(registry->mutex);registry->active.erase(id);});
        if(streamId)data->http2Ctx->onStreamClose(streamId,[connection,responder,streamId]{
            responder.cancel();
            if(auto ptr=connection.lock())if(auto* data=static_cast<HttpConnData*>(ptr->getUserData()))if(data->deferred)data->deferred->pending.erase(streamId);
        });
        try{spec->start(responder);}catch(...){FHttpResponse failed;failed.setStatusCode(StatusCode::_500).setBody("Deferred handler failed");responder.complete(std::move(failed));}
        return true;
    }
    void FHttpServer::drainDeferred() {
        if(FEventLoop::getCurrentLoop())throw std::logic_error("Stop the server from its control thread");
        std::vector<DeferredRegistry::Entry> active;
        {
            std::lock_guard lock(m_deferred->mutex);m_deferred->accepting=false;
            for(auto& [id,entry]:m_deferred->active)active.push_back(entry);
            m_deferred->active.clear();
        }
        std::set<FEventLoop*> loops;
        for(auto& entry:active){loops.insert(entry.loop);entry.loop->AddTask([entry]{
            entry.responder.cancel();
            if(auto ptr=entry.connection.lock())if(auto* data=static_cast<HttpConnData*>(ptr->getUserData()))data->deferred.reset();
        });}
        for(auto* loop:loops){auto done=std::make_shared<std::promise<void>>();auto future=done->get_future();loop->AddTask([done]{done->set_value();});future.get();}
    }

};


const std::unordered_map<std::string, std::string> extensionToContentType =
{
{"html","text/html"},
{"htm","text/html"},
{"shtml","text/html"},
{"css","text/css"},
{"js","application/x-javascript"},
{"xml","text/xml"},
{"gif","image/gif"},
{"jpeg","image/jpeg"},
{"jpg","image/jpeg"},
{"atom","application/atom+xml"},
{"rss","application/rss+xml"},
{"mml","text/mathml"},
{"txt","text/plain"},
{"jad","text/vnd.sun.j2me.app-descriptor"},
{"wml","text/vnd.wap.wml"},
{"htc","text/x-component"},
{"png","image/png"},
{"tif","image/tiff"},
{"tiff","image/tiff"},
{"wbmp","image/vnd.wap.wbmp"},
{"ico","image/x-icon"},
{"jng","image/x-jng"},
{"bmp","image/x-ms-bmp"},
{"svg","image/svg+xml"},
{"webp","image/webp"},
{"jar","application/java-archive"},
{"war","application/java-archive"},
{"ear","application/java-archive"},
{"hqx","application/mac-binhex40"},
{"doc","application/msword"},
{"pdf","application/pdf"},
{"ps","application/postscript"},
{"eps","application/postscript"},
{"ai","application/postscript"},
{"rtf","application/rtf"},
{"xls","application/vnd.ms-excel"},
{"ppt","application/vnd.ms-powerpoint"},
{"wmlc","application/vnd.wap.wmlc"},
{"kml","application/vnd.google-earth.kml+xml"},
{"kmz","application/vnd.google-earth.kmz"},
{"7z","application/x-7z-compressed"},
{"cco","application/x-cocoa"},
{"jardiff","application/x-java-archive-diff"},
{"jnlp","application/x-java-jnlp-file"},
{"run","application/x-makeself"},
{"pl","application/x-perl"},
{"pm","application/x-perl"},
{"prc","application/x-pilot"},
{"pdb","application/x-pilot"},
{"rar","application/x-rar-compressed"},
{"rpm","application/x-redhat-package-manager"},
{"sea","application/x-sea"},
{"swf","application/x-shockwave-flash"},
{"sit","application/x-stuffit"},
{"tcl","application/x-tcl"},
{"tk","application/x-tcl"},
{"der","application/x-x509-ca-cert"},
{"pem","application/x-x509-ca-cert"},
{"crt","application/x-x509-ca-cert"},
{"xpi","application/x-xpinstall"},
{"xhtml","application/xhtml+xml"},
{"zip","application/zip"},
{"bin","application/octet-stream"},
{"exe","application/octet-stream"},
{"dll","application/octet-stream"},
{"deb","application/octet-stream"},
{"dmg","application/octet-stream"},
{"eot","application/octet-stream"},
{"iso","application/octet-stream"},
{"img","application/octet-stream"},
{"msi","application/octet-stream"},
{"msp","application/octet-stream"},
{"msm","application/octet-stream"},
{"mid","audio/midi"},
{"midi","audio/midi"},
{"kar","audio/midi"},
{"mp3","audio/mpeg"},
{"ogg","audio/ogg"},
{"ra","audio/x-realaudio"},
{"3gpp","video/3gpp"},
{"3gp","video/3gpp"},
{"mpeg","video/mpeg"},
{"mpg","video/mpeg"},
{"mov","video/quicktime"},
{"flv","video/x-flv"},
{"mng","video/x-mng"},
{"asx","video/x-ms-asf"},
{"asf","video/x-ms-asf"},
{"wmv","video/x-ms-wmv"},
{"avi","video/x-msvideo"},
{"m4v","video/mp4"},
{"mp4","video/mp4"},
};

