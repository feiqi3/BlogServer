#pragma once

#include "FDef.h"
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"

namespace Blog{
    class FrontGroundController: public Fei::Http::FControllerBase {
        public:
        FrontGroundController();
		Fei::Http::FHttpResponse Articles(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse ArticleDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse Categories(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse CategoriesDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		Fei::Http::FHttpResponse About(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
    };
};