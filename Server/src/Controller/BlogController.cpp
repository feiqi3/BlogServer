#include "BlogController.h"
#include "Core/ApiChangeDataDef.h"
#include "Core/JsonTool.h"
#include "DAO/QueryPosts.h"
#include "Http/FReflect.h"
#include "Model/Posts.h"
#include "Utils/Digital.h"
#include "Utils/HtmlHelper.h"

namespace Blog{

    BlogController::BlogController():Fei::Http::FControllerBase("BlogController"){
    }


Fei::Http::FHttpResponse BlogController::GetBlog(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
    auto idStr = var.get("id");
    Fei::Http::FHttpResponse res;
    if(idStr.empty() || !Digital::isNumber(idStr)){
        res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
        return res;
    }
    uint64_t id = std::stoul(idStr);
    auto postOpt = DAO::PostQuery::QueryPostById(id);
    if(!postOpt.has_value()){
        res.setBody(JsonTool::ToString(getErrorJson("Post not exist")));
        return res;
    }
    Model::Post& post = postOpt.value();
    post.content = html_unescape(post.content);
    auto j = Fei::Http::FReflect::fromClass(post);
    j["result"] = ApiOk;
    res.setBody(JsonTool::ToString(j));
    return res;
}

}