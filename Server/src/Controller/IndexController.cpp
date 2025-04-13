#include "IndexController.h"
#include "Service/QuickRedirect.h"

namespace Blog{
		Fei::Http::FHttpResponse IndexController::toIndex(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&){
            return Redirector::RedirectTo("/message");
        }

}