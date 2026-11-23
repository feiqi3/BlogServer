#include "Core/TemplateRender.h"
#include "Http/FController.h"
#include "Http/FHttpDef.h"
#include "Service/AdminLogin.h"
#include "Utils/FileReader.h"
#include "BackGroundController.h"
#include "Core/Session.h"
#include "Core/ApiChangeDataDef.h"
#include "Service/QuickRedirect.h"
#include <cstdint>
#include <string>
#include <vector>
#include "DAO/QueryPosts.h"
#include "DAO/CategoryQuery.h"
#include "Core/ServerBasicDef.h"
#include "Utils/Digital.h"
namespace Blog{
    Fei::Http::FHttpResponse BackGroundController::ScriptsPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar&) {
        if (!AdminLogin::instance()->isLogin(req)) return Redirector::RedirectTo("/background");
        MemoryMappedFile file(BlogWebPagePath + "backyard-scripts.html", Mode::ReadOnly, 0);
        if (!file.data()) return Redirector::RedirectTo("/404");
        Fei::Http::FHttpResponse response;
        response.addHeader("Cache-Control", "no-store");
        response.setBody(std::string(static_cast<char*>(file.data()), file.size()));
        return response;
    }
    BackGroundController::BackGroundController():Fei::Http::FControllerBase("BackGroundController"){
    }
    Fei::Http::FHttpResponse BackGroundController::LoginPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){

        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(isLogin){
            return Redirector::RedirectTo("/background/articles");
        }

        Fei::Http::FHttpResponse ret;
        MemoryMappedFile file(SERVER_RESOURCE_DIR "web/page/backyard-login.html", Mode::ReadOnly, 0);
        if (!file.data()) {
            ret.setStatusCode(Fei::Http::StatusCode::_404);
            ret.setBody("login page template missing");
            return ret;
        }
        auto d = file.data();
        ret.setBody(std::string((char*)d, file.size()));
        return ret;
    }   


    Fei::Http::FHttpResponse BackGroundController::ArticleListPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(!isLogin){
            return Redirector::RedirectTo("/background");
        }
        std::string pageStr;
        req.getQuery("page", pageStr);

        uint64_t page = 0;
        if(!pageStr.empty()){
            if(!Digital::isNumber(pageStr)){
                Fei::Http::FHttpResponse res;
                res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
                return res;
              }
            page = std::stoull(pageStr);
        }

        constexpr int kPageSize = 25;
        int total = DAO::PostQuery::QueryAllPostCount();
        int totalPages = (total + kPageSize - 1) / kPageSize;
        if(totalPages <= 0){
            totalPages = 1;
        }
        uint64_t curPage = page;
        if(curPage >= (uint64_t)totalPages){
            curPage = totalPages - 1;
        }

        auto posts = DAO::PostQuery::QueryPostsBasicStatusByPage(curPage,kPageSize);
        bool hasPrev = curPage > 0;
        bool hasMore = curPage + 1 < (uint64_t)totalPages;

        uint64_t winStart = 0;
        uint64_t winEnd = totalPages - 1;
        if(totalPages > 9){
            winStart = (curPage > 4) ? curPage - 4 : 0;
            winEnd = winStart + 8;
            if(winEnd >= (uint64_t)totalPages){
                winEnd = totalPages - 1;
                winStart = winEnd - 8;
            }
        }
        nlohmann::json pageArr = nlohmann::json::array();
        for(uint64_t i = winStart; i <= winEnd; ++i){
            pageArr.push_back((int)i);
        }

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
        renderData.renderData["pages"] = pageArr;
        renderData.setData("curPage",(int)curPage);
        renderData.setData("prevPage",(int)curPage - 1);
        renderData.setData("nextPage",(int)curPage + 1);
        renderData.setData("hasPrev",hasPrev);
        renderData.setData("hasMore",hasMore);
        std::string renderRes;
        TemplateRender::instance()->render(BlogWebPagePath + "backyard-articles.html",renderData,renderRes);
        Fei::Http::FHttpResponse response;
        response.setBody(renderRes);
        return response;
    }

    Fei::Http::FHttpResponse BackGroundController::CategoryPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(!isLogin){
            return Redirector::RedirectTo("/background");
        }
        auto beginPost = var.get("id-begin");

        uint64_t lastId = 0;
        if(beginPost.empty()){
            lastId = 0;
        }else{
            if(!Digital::isNumber(beginPost)){
                Fei::Http::FHttpResponse res;
                res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
                return res;
              }
            lastId = std::stoull(beginPost);
        }

        auto categorys = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
        TemplateRenderData renderData;
        struct CategoryProfile{
            uint64_t id;
            std::string name;
        };

        std::vector<CategoryProfile> model;
        for(auto&& i : categorys){
            model.push_back(fromTuple<CategoryProfile>(i));
        }
        renderData.setData("categories",model);
        std::string renderRes;
        TemplateRender::instance()->render(BlogWebPagePath + "backyard-categories.html",renderData,renderRes);
        Fei::Http::FHttpResponse response;
        response.setBody(renderRes);
        return response;
    }

    Fei::Http::FHttpResponse BackGroundController::AlbumPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(!isLogin){
            return Redirector::RedirectTo("/background");
        }
        Fei::Http::FHttpResponse res;
        MemoryMappedFile file(BlogWebPagePath + "backyard-albums.html", Mode::ReadOnly, 0);
        if (file.data() == nullptr) {
            return Redirector::RedirectTo("/404");
        }
        res.setBody(std::string((char*)file.data(), file.size()));
        return res;
    }

    Fei::Http::FHttpResponse BackGroundController::PhotoPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
        bool isLogin = AdminLogin::instance()->isLogin(req);
        if(!isLogin){
            return Redirector::RedirectTo("/background");
        }
        Fei::Http::FHttpResponse res;
        MemoryMappedFile file(BlogWebPagePath + "backyard-photos.html", Mode::ReadOnly, 0);
        if (file.data() == nullptr) {
            return Redirector::RedirectTo("/404");
        }
        res.setBody(std::string((char*)file.data(), file.size()));
        return res;
    }
}
