#ifndef FHTTPSERVER_H
#define FHTTPSERVER_H
#include <memory>
#include "FCallBackDef.h"

#include "FDef.h"
namespace Fei{
	class FTcpServer;
}
namespace Fei::Http {
class FHttpServer {
public:
	FHttpServer(uint32 threadNums);
	void run();
	void stop();
private:
	void handleTcpIn(FTcpConnPtr ptr, FBufferReader& reader);
	void handleTcpConnEstablish(FTcpConnPtr ptr);
private:
	std::unique_ptr<FTcpServer> mTcpServer;
};
} // namespace Fei

#endif