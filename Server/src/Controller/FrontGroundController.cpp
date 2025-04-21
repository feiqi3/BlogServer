#include "FrontGroundController.h"
#include "Core/ServerBasicDef.h"
#include "Core/TemplateRender.h"
#include "DAO/CategoryQuery.h"
#include "Model/Categories.h"
#include "DAO/QueryPosts.h"
#include "Service/QuickRedirect.h"
#include "Utils/Digital.h"
#include "Utils/FileCache.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {
    int sPerPageCount = 10;
}

namespace Blog {
Fei::Http::FHttpResponse
FrontGroundController::Articles(const Fei::Http::FHttpRequest &req,
                                const Fei::Http::FPathVar &var) {
    auto cates = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
    std::map<uint64_t, std::string> catesMap;
    for(auto&& i : cates){
        catesMap.insert({std::get<0>(i),std::move(std::get<1>(i))});
    }
    auto pageNum = var.get("page");
    int page = 0;
    if(!pageNum.empty() && Digital::isNumber(pageNum)){
        page = std::min(std::stoi(pageNum) + 1 ,1);
    }
    auto postVec = DAO::PostQuery::QueryPostDataProfile(page - 1);
    struct BlogProfile{
        uint64_t id;
        std::string title;
        std::string profile;
        std::string blogtitlepic;
        std::string categoryName;
        uint64_t created_at;
    };
    std::vector<BlogProfile> prf;
    prf.reserve(postVec.size());
    for(auto && p : postVec){
        BlogProfile profile{
            .id = std::get<0>(p),
            .title = std::move(std::get<1>(p)),
            .profile = std::move(std::get<2>(p)),
            .blogtitlepic = std::move(std::get<5>(p)),
            .categoryName = catesMap[std::get<6>(p)],
            .created_at =  std::get<3>(p),
        };
        prf.push_back(std::move(profile));
    }

    TemplateRenderData data;
    int totalPageNum = std::max(1,int(std::ceil((float)DAO::PostQuery::QueryPostCount() / sPerPageCount)));
    //1. generate json
    data.setData("pageForward",std::max(page - 1,1));
    data.setData("pageNext",std::min(page + 1,totalPageNum));
    data.setData("radius",(2));
    data.setData("currentPage", (page));
    data.setData("pageNums", totalPageNum - 1);
    data.setData("posts",std::move(prf));
    TemplateRender render;
    std::string returnBody;
    render.render(BlogWebPagePath + "index.html", data, returnBody);
    Fei::Http::FHttpResponse res;
    res.setBody(returnBody);
    return res;

}

Fei::Http::FHttpResponse FrontGroundController::ArticleDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto _id = var.get("id");
    int id = 0;
    if(!_id.empty() && Digital::isNumber(_id)){
        id = std::stoi(_id);
    }

    struct BlogPost{
        std::string titlepic;
        std::string title;
        uint64_t created_at;
        uint64_t updated_at;
        std::string categoryName;
        std::string content;
    };

    auto post = DAO::PostQuery::QueryPostById(id);
    if(!post.has_value()){
        return Redirector::RedirectTo("/404");
    }
    auto& _post  = post.value();
    
    auto cateName = DAO::CategoryQuery::QueryCategoryNameById(_post.category_id);
    if(!cateName.has_value()){
        return Redirector::RedirectTo("/505");
    }
    
    BlogPost mPost{
        .titlepic = std::move(_post.titlepic),
        .title = std::move(_post.title),
        .created_at = _post.created_at,
        .updated_at = _post.updated_at,
        .categoryName = std::move(cateName.value()),
        .content = std::move(_post.content)
    };
    TemplateRenderData data;
    data.setData("post",std::move(mPost));
    TemplateRender render;
    std::string renderOut;
    render.render(BlogWebPagePath + "article.html", data, renderOut);
    Fei::Http::FHttpResponse res;
    res.setBody(renderOut);
    return res;
}

} // namespace Blog