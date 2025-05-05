#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"

namespace Blog {
	class ServerShutdownController : public Fei::Http::FControllerBase {
	public:
    ServerShutdownController() :FControllerBase("ServerShutdownController") {}

		Fei::Http::FHttpResponse Shutdown(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
		

		REGISTER_MAPPING_BEGIN("")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/shutdown", ServerShutdownController, Shutdown);
			REGISTER_MAPPING_END

	};

	REGISTER_CONTROLLER_CLASS(ServerShutdownController)
}