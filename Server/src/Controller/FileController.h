#pragma once


#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"

namespace Blog {
	class FileController : public Fei::Http::FControllerBase {
	public:
		FileController() :FControllerBase("File") {}

		Fei::Http::FHttpResponse getFile(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);

		REGISTER_MAPPING_BEGIN("/file")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/{name}", FileController, getFile);
		REGISTER_MAPPING_END

	};

	REGISTER_CONTROLLER_CLASS(FileController)
}