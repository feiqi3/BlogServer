#include "Core/TemplateRender.h"
#include "Http/FController.h"
#include "Service/AdminLogin.h"
#include "Utils/FileReader.h"
#include "BackGroundController.h"
#include "Core/Session.h"
#include "Service/QuickRedirect.h"
#include <cstdint>
#include <string>
#include <vector>
#include "DAO/QueryPosts.h"
#include "Core/ServerBasicDef.h"

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
        auto beginPost = var.get("id-begin");

        uint64_t lastId = 0;
        if(beginPost.empty()){
            lastId = 0;
        }else{
            lastId = std::stoull(beginPost);
        }

        auto posts = DAO::PostQuery::QueryPostsBasicStatusByPage(lastId,25);
        TemplateRenderData renderData;
        struct BasicBlogProfile{
            uint64_t id;
            std::string title;
            uint64_t created_at;
            uint64_t updated_at;
        };

        std::vector<BasicBlogProfile> model;
        for(auto&& i : posts){
            model.push_back(fromTuple<BasicBlogProfile>(i));
        }
        renderData.setData("posts",model);
        TemplateRender render;
        std::string renderRes;
        render.render(BlogWebPagePath + "backyard-articles.html",renderData,renderRes);
        Fei::Http::FHttpResponse response;
        response.setBody(renderRes);
        return response;
    }

}