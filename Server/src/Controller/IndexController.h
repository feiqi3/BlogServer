#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"

namespace Blog {
	class IndexController : public Fei::Http::FControllerBase {
	public:
    IndexController() :FControllerBase("IndexController") {}

		Fei::Http::FHttpResponse toIndex(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
		

		REGISTER_MAPPING_BEGIN("/")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "", IndexController, toIndex);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "page={page}", IndexController, toIndex);
			REGISTER_MAPPING_END

	};

	REGISTER_CONTROLLER_CLASS(IndexController)
}