#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"

namespace Blog {
	class DatabaseTestPageController : public Fei::Http::FControllerBase {
	public:
    DatabaseTestPageController():FControllerBase("DatabaseTestPageController") {}

		Fei::Http::FHttpResponse PostMessage(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
        Fei::Http::FHttpResponse GetMessageByPage(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
        Fei::Http::FHttpResponse GetMessagePage(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
		REGISTER_MAPPING_BEGIN("/test")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "/message", DatabaseTestPageController, PostMessage);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/message/page={page}", DatabaseTestPageController, GetMessageByPage);
			REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/message", DatabaseTestPageController, GetMessagePage);
            REGISTER_MAPPING_END
	
	};

	REGISTER_CONTROLLER_CLASS(DatabaseTestPageController)
}