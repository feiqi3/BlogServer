#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"
#include <string>

namespace Blog{
    class CategoryController : public Fei::Http::FControllerBase {
        public:
		CategoryController();

        Fei::Http::FHttpResponse GetCategory(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
        Fei::Http::FHttpResponse GetAllCategories(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		REGISTER_MAPPING_BEGIN("/api")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/category/{id}", CategoryController, GetCategory);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/categories", CategoryController, GetAllCategories);
            REGISTER_MAPPING_END

        private:

    };
    REGISTER_CONTROLLER_CLASS(CategoryController)
}
