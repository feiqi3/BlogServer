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

        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(isLogin){
            return Redirector::RedirectTo("/background/articles");
        }

        Fei::Http::FHttpResponse ret;
        MemoryMappedFile file(SERVER_RESOURCE_DIR "web/page/backyard-login.html", Mode::ReadOnly, 0);
        auto d = file.data();
        ret.setBody(std::string((char*)d, file.size()));
        return ret;
    }   


    Fei::Http::FHttpResponse BackGroundController::ArticleListPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(!isLogin){
            return Redirector::RedirectTo("/background");
        }

        //
    }

}