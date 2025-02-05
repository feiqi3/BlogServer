#pragma once

#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"



namespace Blog {
	class Err404Controller : public Fei::Http::FControllerBase {
	public:
		Err404Controller():FControllerBase("404") {}

		Fei::Http::FHttpResponse Err404(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
	
		REGISTER_MAPPING_BEGIN("")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/404", Err404Controller, Err404);
		REGISTER_MAPPING_END
	
	};

	REGISTER_CONTROLLER_CLASS(Err404Controller)
}

