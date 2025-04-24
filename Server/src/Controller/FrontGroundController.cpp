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
    DAO::PostQuery::updateViewTimes(id);
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
    setBlogCommonData(data);
    render.render(BlogWebPagePath + "article.html", data, renderOut);
    Fei::Http::FHttpResponse res;
    res.setBody(renderOut);
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::Categories(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){

    auto _cates = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
    if(_cates.empty()){
        return Redirector::RedirectTo("/404");
    }
    
    TemplateRenderData data;
    data.setData("categories",std::move(DAO::CategoryQuery::QueryAllCategory()));
    TemplateRender render;
    std::string renderOut;
    setBlogCommonData(data);
    render.render(BlogWebPagePath + "category.html", data, renderOut);
    Fei::Http::FHttpResponse res;
    res.setBody(renderOut);
    return res;
}

Fei::Http::FHttpResponse  FrontGroundController::CategoriesDetail(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto _id = var.get("id");
    if(!Digital::isNumber(_id)){
        return Redirector::RedirectTo("/404");
    }
    uint64_t id = std::stoull(_id);
    
    auto _cate = DAO::CategoryQuery::QueryCategoryById(id);
    if(!_cate.has_value()){
        return Redirector::RedirectTo("/404");
    }
    auto& cate = _cate.value();
    auto& cateName = (cate.name);

    auto pageNum = var.get("page");
    int page = 0;
    if(!pageNum.empty() && Digital::isNumber(pageNum)){
        page = std::min(std::stoi(pageNum)  ,0);
    }
    auto postVec = DAO::PostQuery::QueryPostDataProfileByCategoryId(cate.id,page );
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
            .categoryName = cateName,
            .created_at =  std::get<3>(p),
        };
        prf.push_back(std::move(profile));
    }

    TemplateRenderData data;
    int totalPageNum = std::max(1,int(std::ceil((float)DAO::PostQuery::QueryPostOfCategoryCount(cate.id) / sPerPageCount)));
    //1. generate json
    setPageData(data, page, totalPageNum, 2);
    setBlogCommonData(data);
    data.setData("posts",std::move(prf));
    data.setData("category",cate);
    TemplateRender render;
    std::string returnBody;
    render.render(BlogWebPagePath + "category-detail.html", data, returnBody);
    Fei::Http::FHttpResponse res;
    res.setBody(returnBody);
    return res;
}

} // namespace Blog