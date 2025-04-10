#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"
#include <string>

namespace Blog{
    class BlogController : public Fei::Http::FControllerBase {
        public:
		BlogController();

        Fei::Http::FHttpResponse GetBlog(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
		REGISTER_MAPPING_BEGIN("/api")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/blog/{id}", BlogController, GetBlog);
            REGISTER_MAPPING_END

        private:

    };
    REGISTER_CONTROLLER_CLASS(BlogController)
}
