#include "Http/FController.h"
#include "Service/AdminLogin.h"
#include "Utils/FileReader.h"
#include "BackGroundController.h"
#include "Core/Session.h"
#include "Service/QuickRedirect.h"
#include <string>
namespace Blog{
    BackGroundController::BackGroundController():Fei::Http::FControllerBase("BackGroundController"){
    }
    Fei::Http::FHttpResponse BackGroundController::LoginPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){

        auto cookieNum = req.getCookieSize();
        std::string sessionId;
        for(auto i = 0; i < cookieNum; i++){
            auto& cookie = req.getCookie(i);
            if(cookie.getValue("sessionId", sessionId))
            {
                break;
            }
        }
        if(!sessionId.empty()){
            auto admin = AdminLogin::instance();
            bool islogin = admin->isLogin(sessionId);
            if(islogin){
                return Redirector::RedirectTo("/404");
            }
        }


        Fei::Http::FHttpResponse ret;
        MemoryMappedFile file(SERVER_RESOURCE_DIR "web/page/backyard-login.html", Mode::ReadOnly, 0);
        auto d = file.data();
        ret.setBody(std::string((char*)d, file.size()));
        return ret;
    }   

}