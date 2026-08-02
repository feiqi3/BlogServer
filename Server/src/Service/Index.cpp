#include "Index.h"
#include "Core/ServerBasicDef.h"
#include "Service/BlogCommonDataSetter.h"
#include "Utils/Digital.h"
#include <string>
#include "Core/TemplateRender.h"
#include "DAO/QueryPosts.h"
#include "DAO/CategoryQuery.h"

namespace Blog{


Fei::Http::FHttpResponse IndexArticles(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto cates = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
    std::map<uint64_t, std::string> catesMap;
    for(auto&& i : cates){
        catesMap.insert({std::get<0>(i),std::move(std::get<1>(i))});
    }
    auto pageNum = var.get("page");
    int page = 1;
    if(!pageNum.empty() && Digital::isNumber(pageNum)){
        page = std::max(std::stoi(pageNum),0);
    }
    auto postVec = DAO::PostQuery::QueryPostDataProfile(page -1  );
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
            .categoryName = catesMap[std::get<6>(p)],
            .created_at =  std::get<3>(p),
            .viewTimes = std::get<7>(p),
        };
        prf.push_back(std::move(profile));
    }

    TemplateRenderData data;
    int totalPageNum = std::max(1,int(std::ceil((float)DAO::PostQuery::QueryPostCount() / sPerPageCount)));
    //1. generate json
    setPageData(data, page, totalPageNum, 2);
    updateAndsetBlogCommonData(data);
    data.setData("posts",std::move(prf));
    std::string returnBody;
    TemplateRender::instance()->render(BlogWebPagePath + "index.html", data, returnBody);
    Fei::Http::FHttpResponse res;
    res.setBody(returnBody);
    return res;
}

}