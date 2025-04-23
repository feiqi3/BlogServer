#include "FrontGroundController.h"
#include "Core/ServerBasicDef.h"
#include "Core/TemplateRender.h"
#include "DAO/CategoryQuery.h"
#include "Model/Categories.h"
#include "Service/BlogCommonDataSetter.h"
#include "DAO/QueryPosts.h"
#include "Service/Index.h"
#include "Service/QuickRedirect.h"
#include "Utils/Digital.h"
#include "Utils/FileCache.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Blog {
Fei::Http::FHttpResponse
FrontGroundController::Articles(const Fei::Http::FHttpRequest &req,
                                const Fei::Http::FPathVar &var) {
    return IndexArticles(req, var);
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
        uint32_t viewTimes;
        
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