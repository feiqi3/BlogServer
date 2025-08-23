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
#include <algorithm>
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

	void FHttpServer::initSSLenv(const std::string& certFile,const std::string& privateKey){
		FTcpServer::initGlobalSSLEnv(certFile, privateKey);
	}

	void FHttpServer::deinitSSLenv(){
		FTcpServer::deinitGlobalSSLEnv();		
	}

	FHttpServer::FHttpServer(uint32 threadNums) : mTcpServer(std::make_unique<FTcpServer>(threadNums))
	{
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
		}
	}

	FHttpServer::~FHttpServer() {
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
	}

	void FHttpServer::stop(bool force)
	{
		mTcpServer->stop(force);
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
			int h2ConnIdleMulti = 2;
			bool firstTimeOut = false;
			if (mHttpRequestWaitTime >= 0 && now - connData->hasSurvivedTime > mHttpRequestWaitTime * h2ConnIdleMulti) {
				firstTimeOut = true;
			}

			if (mHttpConnectionTimeout >= 0 && now - connData->hasSurvivedTime > mHttpConnectionTimeout * h2ConnIdleMulti) {
				isTimeOut = true;
			}

			if (isTimeOut) {
				if (isTimeOut) {
					Logger::instance()->log(MODULE_NAME, lvl::info, "connection timeout from {}.{}.{}.{} : {} error.", ptr->getAddr().un.un_byte.a0, ptr->getAddr().un.un_byte.a1, ptr->getAddr().un.un_byte.a2, ptr->getAddr().un.un_byte.a3, ptr->getAddr().port);
					ptr->forceClose();
				}
			}
			else if (firstTimeOut) {
				auto& h2 = connData->http2Ctx;
				h2->http2SubmitGoaway();
				auto sendNum = h2->http2SendProcess();
				if (sendNum > 0) {
					auto reader = h2->getSendBufferReader();
					
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
		if (http_data->hasSurvivedTime == 0) {
			http_data->hasSurvivedTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}
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
		ptr->send(std::move(response.toString()));
	}

	void FHttpServer::http2Process(const FTcpConnPtr& ptr, FBufferReader& reader)
	{
		assert(ptr->isHttp2());

		//1. process data
		auto http_data = getDataFromTcpConn(ptr);
		if (http_data->hasSurvivedTime == 0) {
			http_data->hasSurvivedTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}
		auto& http2Ctx = http_data->http2Ctx;
		http2Ctx->http2RecvProcess(reader);

		if (http2Ctx->isConnEnd()) {
			ptr->forceClose();
			return;
		}

		auto perStream = [&ptr,this,&http_data](const FHttpRequest& request,uint32_t streamID) {
			auto response = httpHandle(ptr, request);
			http_data->http2Ctx->http2SubmitResponseStream(streamID,response,true);
			return true;
		};
		http2Ctx->traversalFinishedStreams(perStream);
		auto sendNum = http2Ctx->http2SendProcess();
		if (sendNum == 0)return; //Window control?
		auto sreader = http2Ctx->getSendBufferReader();
		int dataSize = 0;
		auto dataPtr = sreader.peekAll(dataSize);
		ptr->send((const char*)dataPtr,dataSize);
		sreader.expireSize(dataSize);
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

		preProcessHttpRequestSend(ptr, request, response);
		return response;
	}

};


const std::unordered_map<std::string, std::string> extensionToContentType =
{
{"html","text/html"},
{"htm","text/html"},
{"shtml","text/html"},
{"css","text/css"},
{"xml","text/xml"},
{"gif","image/gif"},
{"jpeg","image/jpeg"},
{"jpg","image/jpeg"},
{"js","application/x-javascript"},
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

