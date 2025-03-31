#include "Http/FController.h"
#include "Utils/FileReader.h"
#include "BackGroundController.h"
#include "Core/Session.h"
#include "Service/QuickRedirect.h"
namespace Blog{
    BackGroundController::BackGroundController():Fei::Http::FControllerBase("BackGroundController"){
    }
    Fei::Http::FHttpResponse BackGroundController::LoginPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        
        std::string cookie;
        req.getHeader("Cookie", cookie);
        const std::string sessionId = "sessionId=";
        auto pos = cookie.find_first_of(sessionId);
        if(pos != std::string::npos){
            auto end = cookie.find_first_of(";", pos);
            if(end == std::string::npos)end = cookie.size();
            auto id = cookie.substr(pos + sessionId.size(), end - pos - sessionId.size());
            if(SessionManager::instance()->hasSession(id)){
                return Redirector::RedirectTo("/index.html");
            }
        }

        Fei::Http::FHttpResponse ret;
        MemoryMappedFile file(SERVER_RESOURCE_DIR "web/page/backyard-login.html", Mode::ReadOnly, 0);
        auto d = file.data();
        ret.setBody(std::string((char*)d, file.size()));
        return ret;
    }   

}