#include "IndexController.h"
#include "Service/Index.h"
#include "Service/QuickRedirect.h"

namespace Blog{
		Fei::Http::FHttpResponse IndexController::toIndex(const Fei::Http::FHttpRequest&req, const Fei::Http::FPathVar& var){
            return IndexArticles(req,var);
        }

}