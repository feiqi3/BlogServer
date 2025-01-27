#include "Http/FHttpServer.h"
#include "Http/FPathVar.h"
#include "Http/FController.h"
#include "FeiLibIniter.h"
#include <string>
#include "cstring"
#include <chrono>
#include <thread>

using namespace Fei::Http;

class ControllerValue : public Fei::Http::FControllerBase {
public:
	ControllerValue():FControllerBase("ControllerTest") {
	}

	//GET /value/{val}
	Fei::Http::FHttpResponse GetValueInPath(const FHttpRequest& request, const FPathVar& vars) {
		std::stringstream ss;
		ss << "Get value: " << vars.get("val");
		FHttpResponse res{};
		res.setBody(ss.str());
		return res;
	}
	REGISTER_MAPPING_BEGIN("/")
		REGISTER_MAPPING_FUNC(Method::GET,"{val}", ControllerValue, GetValueInPath)
	REGISTER_MAPPING_END
};

REGISTER_CONTROLLER_CLASS(ControllerValue)
void HttpServerRun();

int main() { HttpServerRun(); }

void HttpServerRun()
{
	using namespace Fei::Http;
	Fei::FeiLibInit();
	FHttpServer* server = new FHttpServer(4);

	server->addListenPort(80);
	server->run();
	while (1) {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(100ms);
	}
	Fei::FeiLibUnInit();
	delete server;
}
