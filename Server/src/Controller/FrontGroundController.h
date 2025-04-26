#pragma once

#include "FDef.h"
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"

namespace Blog{
    class FrontGroundController: public Fei::Http::FControllerBase {
        public:
        FrontGroundController():Fei::Http::FControllerBase("FrontGroundController"){}
		Fei::Http::FHttpResponse Articles(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse ArticleDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse Categories(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse CategoriesDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse About(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		REGISTER_MAPPING_BEGIN("")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/realIndex={page}", FrontGroundController, Articles);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/post/{id}", FrontGroundController, ArticleDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/category/id={id}&page={page}", FrontGroundController, CategoriesDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/category/id={id}", FrontGroundController, CategoriesDetail);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/categories", FrontGroundController, Categories);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/about", FrontGroundController, About);
			REGISTER_MAPPING_END
    };
	REGISTER_CONTROLLER_CLASS(FrontGroundController);
};
