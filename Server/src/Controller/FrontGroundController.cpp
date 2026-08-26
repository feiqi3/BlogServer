#include "FrontGroundController.h"
#include "FConfigReader.h"
#include "Core/ApiChangeDataDef.h"
#include "Core/JsonTool.h"
#include "Core/ServerBasicDef.h"
#include "Core/TemplateRender.h"
#include "DAO/CategoryQuery.h"
#include "Model/Categories.h"
#include "Service/BlogCommonDataSetter.h"
#include "DAO/QueryPosts.h"
#include "Service/Index.h"
#include "Service/QuickRedirect.h"
#include "Service/RssBuilder.h"
#include "Utils/Digital.h"
#include "Utils/FileReader.h"
#include "Utils/FileCache.h"
#include "Utils/TimeHelper.h"
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

    auto post = DAO::PostQuery::QueryPostById(id);
    if(!post.has_value()){
        return Redirector::RedirectTo("/404");
    }
    DAO::PostQuery::updateViewTimes(id);
    return RenderArticlePost(post.value(), "/post/" + std::to_string(id));
}

Fei::Http::FHttpResponse FrontGroundController::RenderArticlePost(const Model::Post& _post, const std::string& pageUrl){
    auto cateName = DAO::CategoryQuery::QueryCategoryNameById(_post.category_id);
    if(!cateName.has_value()){
        return Redirector::RedirectTo("/505");
    }

    struct BlogPost{
        uint64_t id;
        std::string profile;
        std::string titlepic;
        std::string title;
        uint64_t created_at;
        uint64_t updated_at;
        std::string categoryName;
        std::string content;
        uint32_t viewTimes;
        int allowComment;

    };

    BlogPost mPost{
        .id = _post.id,
        .profile = _post.profile,
        .titlepic = _post.titlepic,
        .title = _post.title,
        .created_at = _post.created_at,
        .updated_at = _post.updated_at,
        .categoryName = cateName.value(),
        .content = _post.content,
        .viewTimes = static_cast<uint32_t>(_post.view_times),
        .allowComment = _post.allow_comment
    };
    TemplateRenderData data;
    data.setData("post",std::move(mPost));
    data.setData("pageUrl", pageUrl);
    std::string renderOut;
    setBlogCommonData(data);
    auto gcUrl = Fei::FConfigReader::instance()->getCfg("GoatCounterUrl");
    if (gcUrl.has_value() && !gcUrl->empty()) {
        data.setData("goatcounter_url", gcUrl.value());
    }
    TemplateRender::instance()->render(BlogWebPagePath + "article.html", data, renderOut);
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
    std::string renderOut;
    setBlogCommonData(data);
    TemplateRender::instance()->render(BlogWebPagePath + "category.html", data, renderOut);
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
    int page = 1;
    if(!pageNum.empty() && Digital::isNumber(pageNum)){
        page = std::max(std::stoi(pageNum)  ,0);
    }
    auto postVec = DAO::PostQuery::QueryPostDataProfileByCategoryId(cate.id,page - 1);
    struct BlogProfile{
        uint64_t id;
        std::string title;
        std::string profile;
        std::string blogtitlepic;
        std::string categoryName;
        uint64_t created_at;
        uint64_t viewTimes;
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
            .viewTimes = std::get<6>(p),
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
    std::string returnBody;
    TemplateRender::instance()->render(BlogWebPagePath + "category-detail.html", data, returnBody);
    Fei::Http::FHttpResponse res;
    res.setBody(returnBody);
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::About(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto cfg = Fei::FConfigReader::instance();
    auto titleOpt = cfg->getCfg("AboutPageTitle");
    std::string title = titleOpt.value_or("关于网站");
    auto post = DAO::PostQuery::QueryPostByTitle(title.c_str());
    if(!post.has_value()){
        return Redirector::RedirectTo("/404");
    }
    return RenderArticlePost(post.value(), "/about");
}

Fei::Http::FHttpResponse FrontGroundController::Links(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto cfg = Fei::FConfigReader::instance();
    auto titleOpt = cfg->getCfg("LinksPageTitle");
    std::string title = titleOpt.value_or("友链");
    auto post = DAO::PostQuery::QueryPostByTitle(title.c_str());
    if(!post.has_value()){
        return Redirector::RedirectTo("/404");
    }
    return RenderArticlePost(post.value(), "/links");
}

Fei::Http::FHttpResponse FrontGroundController::Archive(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    Fei::Http::FHttpResponse res;
    auto postVec = DAO::PostQuery::QueryPostsForArchive();
    nlohmann::json j;
    j["result"] = ApiOk;
    nlohmann::json groupsArr = nlohmann::json::array();
    // Group by year-month; std::map keeps keys ascending.
    std::map<std::string, nlohmann::json> monthMap;
    for (auto& p : postVec) {
        uint64_t created_at = std::get<4>(p);
        std::string ym = TimeHelper::toFormatTime(created_at, "%Y-%m");
        nlohmann::json postObj;
        postObj["id"] = std::get<0>(p);
        postObj["title"] = std::get<1>(p);
        postObj["profile"] = std::get<2>(p);
        postObj["titlepic"] = std::get<3>(p);
        postObj["createdAt"] = created_at;
        postObj["viewTimes"] = std::get<5>(p);
        auto it = monthMap.find(ym);
        if (it == monthMap.end()) {
            nlohmann::json g;
            g["year"] = std::stoi(TimeHelper::toFormatTime(created_at, "%Y"));
            g["month"] = std::stoi(TimeHelper::toFormatTime(created_at, "%m"));
            g["posts"] = nlohmann::json::array();
            it = monthMap.insert({ym, g}).first;
        }
        it->second["posts"].push_back(postObj);
    }
    // Reverse iterate for newest-first groups.
    for (auto it = monthMap.rbegin(); it != monthMap.rend(); ++it) {
        groupsArr.push_back(it->second);
    }
    j["groups"] = groupsArr;
    res.setBody(JsonTool::ToString(j));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::PhotosPage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    Fei::Http::FHttpResponse res;
    MemoryMappedFile file(BlogWebPagePath + "photos.html", Mode::ReadOnly, 0);
    if (file.data() == nullptr) {
        return Redirector::RedirectTo("/404");
    }
    res.setBody(std::string((char*)file.data(), file.size()));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::ArchivePage(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    Fei::Http::FHttpResponse res;
    MemoryMappedFile file(BlogWebPagePath + "archive.html", Mode::ReadOnly, 0);
    if (file.data() == nullptr) {
        return Redirector::RedirectTo("/404");
    }
    res.setBody(std::string((char*)file.data(), file.size()));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::RssXml(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    Fei::Http::FHttpResponse res;
    std::string lastModified;
    const std::string& xml = RssBuilder::instance()->getRssXml(lastModified);

    // HTTP/1.1 keeps original case; HTTP/2 header names are lowercase.
    std::string ifModifiedSince;
    if (!req.getHeader("If-Modified-Since", ifModifiedSince)) {
        req.getHeader("if-modified-since", ifModifiedSince);
    }
    if (!ifModifiedSince.empty() && ifModifiedSince == lastModified) {
        res.setStatusCode(Fei::Http::StatusCode::_304);
        return res;
    }

    res.setContentType("application/rss+xml; charset=utf-8");
    res.addHeader("Last-Modified", lastModified);
    res.setBody(xml);
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::RobotsTxt(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    MemoryMappedFile file(BlogWebAssetsPath + "robots.txt", Mode::ReadOnly, 0);
    Fei::Http::FHttpResponse res;
    res.setContentType("text/plain; charset=utf-8");
    res.setBody(std::string(static_cast<const char*>(file.data()), file.size()));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::SitemapXml(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    MemoryMappedFile file(BlogWebAssetsPath + "sitemap.xml", Mode::ReadOnly, 0);
    Fei::Http::FHttpResponse res;
    res.setContentType("application/xml; charset=utf-8");
    res.setBody(std::string(static_cast<const char*>(file.data()), file.size()));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::WebManifest(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    MemoryMappedFile file(BlogWebAssetsPath + "site.webmanifest", Mode::ReadOnly, 0);
    Fei::Http::FHttpResponse res;
    res.setContentType("application/manifest+json; charset=utf-8");
    res.setBody(std::string(static_cast<const char*>(file.data()), file.size()));
    return res;
}

Fei::Http::FHttpResponse FrontGroundController::FaviconIco(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    MemoryMappedFile file(BlogWebAssetsPath + "favicon.ico", Mode::ReadOnly, 0);
    Fei::Http::FHttpResponse res;
    res.setContentType("image/x-icon");
    res.setBody(std::string(static_cast<const char*>(file.data()), file.size()));
    return res;
}

} // namespace Blog
