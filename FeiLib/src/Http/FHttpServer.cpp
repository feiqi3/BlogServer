#include <sstream>
#include "Http/FHttpServer.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "FTCPServer.h"
#include "FTCPConnection.h"
#include "Http/FRouter.h"
#include "FLogger.h"
#include <algorithm>
#define MODULE_NAME "HttpServer"
#define TCP_TIMEOUT 60
#define TCP_INTERVAL 5
#define ERROR_ROUTE_PATH "/error"
#define DEFAULT_CONTENT_TYPE "text/html"
#define DEFAULT_CHAT_SET "; charset=UTF-8"
#define DEFAULT_HTTP_VERSION Version::Http11

static const std::string DefaultCharSet = DEFAULT_CHAT_SET;
static const std::string DefaultContentType = DEFAULT_CONTENT_TYPE + DefaultCharSet;
static const std::string DefaultServerName = "by FeiLib";
extern const std::unordered_map<std::string, std::string> extensionToContentType;

namespace {

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
	FHttpServer::FHttpServer(uint32 threadNums) : mTcpServer(std::make_unique<FTcpServer>(threadNums))
	{
		if (!FRouter::valid()) {
			new FRouter;
		}
		mTcpServer->setOnConnEstablisedCallback(std::bind(&FHttpServer::handleTcpConnEstablish, this,std::placeholders::_1));
		mTcpServer->setOnMessageCallback(std::bind(&FHttpServer::handleTcpIn, this, std::placeholders::_1, std::placeholders::_2));
		mTcpServer->setOnCloseCallback(std::bind( & FHttpServer::handleTcpConnClosed,this,std::placeholders::_1 ));
	}

	FHttpServer::~FHttpServer() {
		if (FRouter::valid()) {
			delete FRouter::instance();
		}
	}

	void FHttpServer::addListenPort(uint32 port)
	{
		this->mTcpServer->addListenPort(port,true);
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

	bool FHttpServer::getContentTypeByPath(const std::string& path, std::string& extensionName)
	{
		return _getContentTypeByPath(path, extensionName);
	}

	void FHttpServer::handleTcpIn(const FTcpConnPtr& ptr, FBufferReader& reader)
	{
		FHttpRequest request(reader);
		
		preProcessTcpConn(ptr, request);

		FRouter::RouteResult routeResult;
		bool notMatchError = false;
		bool isFiltered = false;
		auto addr = ptr->getAddr();
		request.setAddrIn(addr);
		request.setAddrHost(ptr->getAddrAccept());
		FHttpResponse response;
		if (mConnFilterFunc && mConnFilterFunc(request, response)) {
			isFiltered = true;
		}
		
		if (!request.isValid()) {
			Logger::instance()->log(MODULE_NAME, lvl::info, "request error from {}.{}.{}.{} : {} error.",addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3, addr.port);
			notMatchError = true;
		}
		else if(!isFiltered) {
			routeResult = FRouter::instance()->route(request.getMethod(), request.getPath());
			if(!routeResult.isvalid()){
				notMatchError = true;
			}
		}

		if (notMatchError) {
			routeResult = FRouter::instance()->route(Method::GET,ERROR_ROUTE_PATH);
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
				Logger::instance()->log("FHttpServer", lvl::err, "Request handle error: request path: \"{}\", Reason: {}", request.getPath(),exception.what());
				if (mInternalErrCallback) {
					mInternalErrCallback(request, response, exception);
				}
				else {
					defaultExceptionFunc(request, response, exception);
				}
			}catch (std::exception& e){
				Logger::instance()->log("FHttpServer", lvl::err, "Request handle error: request path: \"{}\", Reason: {}",request.getPath(),e.what());
				FException exception;
				defaultExceptionFunc(request, response, exception);
			}
		}

		preProcessHttpRequestSend(ptr, request, response);
		ptr->send(std::move(response.toString()));
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

	}

	void FHttpServer::preProcessTcpConn(const FTcpConnPtr& ptr, const FHttpRequest& request)
	{
		std::string headerAttracted;
		auto addr = ptr->getAddr();
		Logger::instance()->log("FHttpServer", lvl::trace, "Http conntection established, from {}.{}.{}.{} : {}", addr.un.un_byte.a0, addr.un.un_byte.a1, addr.un.un_byte.a2, addr.un.un_byte.a3, addr.port);
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

		if (setKeepAlive) {
			ptr->setKeepAlive(true);
			ptr->setKeepIdle(TCP_TIMEOUT);
			ptr->setKeepInterval(TCP_INTERVAL);
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

