#include "FHTTPServer.h"
#include "FTCPConnection.h"
#include "FRouter.h"
#include "FLogger.h"
#define MODULE_NAME "FHttpServer"

Fei::Http::FHttpServer::FHttpServer(uint32 threadNums) : mTcpServer(std::make_unique<FTcpServer>(threadNums))
{

}

void Fei::Http::FHttpServer::handleTcpIn(FTcpConnPtr ptr, FBufferReader& reader)
{
	auto httpRequest = FHttpRequest(reader);
	if (!httpRequest.isValid()) {
		char buffer[2048];
		buffer[2047] = '\0';
		reader.readTo(buffer, 2047);
		Logger::instance()->log(MODULE_NAME, lvl::warn, "Wired Http in request: \n{}", buffer);
	}
	auto routerMatch = FRouter::instance()->route(httpRequest.getMethod(), httpRequest.getPath());
	if (routerMatch.isvalid()) {
		routerMatch.controllerFunc(httpRequest, routerMatch.pathVariable);
	}
	else {
		Logger::instance()->log(MODULE_NAME, lvl::trace, "Unknown request -> Method: {}, Path: {}",methodToStr(httpRequest.getMethod()),httpRequest.getPath());
	}
}

void Fei::Http::FHttpServer::handleTcpConnEstablish(FTcpConnPtr ptr)
{
	
}
