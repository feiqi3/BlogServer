#include "CategoryController.h"
#include "Core/ApiChangeDataDef.h"
#include "Core/JsonTool.h"
#include "DAO/CategoryQuery.h"
#include "Http/FReflect.h"
#include "Model/Categories.h"
#include "Model/Posts.h"
#include "Utils/Digital.h"
#include "nlohmann/json_fwd.hpp"
namespace Blog {

CategoryController::CategoryController()
    : Fei::Http::FControllerBase("CategoryController") {}

Fei::Http::FHttpResponse
CategoryController::GetAllCategories(const Fei::Http::FHttpRequest &req,
                                     const Fei::Http::FPathVar &var) {
    auto ret = DAO::CategoryQuery::QueryAllCategoryBasicInfo();
    Fei::Http::FHttpResponse res;
    nlohmann::json json;
    json["categories"] = nlohmann::json::array();
    for (auto &v : ret) {
        auto id = std::get<0>(v);
        auto name = std::get<1>(v);
        nlohmann::json j;
        j["id"] = id;
        j["name"] = name;
        json["categories"].push_back(j);
    }
    json["result"] = ApiOk;
    res.setBody(JsonTool::ToString(json));
    return res;
}

Fei::Http::FHttpResponse
CategoryController::GetCategory(const Fei::Http::FHttpRequest &req,
                                const Fei::Http::FPathVar &var) {
  auto idStr = var.get("id");
  Fei::Http::FHttpResponse res;
  if (idStr.empty() || !Digital::isNumber(idStr)) {
    res.setBody(JsonTool::ToString(getErrorJson("Id Error")));
    return res;
  }
  uint64_t id = std::stoul(idStr);
  auto cateOpt = DAO::CategoryQuery::QueryCategoryById(id);
  if (!cateOpt.has_value()) {
    res.setBody(JsonTool::ToString(getErrorJson("Post not exist")));
    return res;
  }
  Model::Category cate = cateOpt.value();
  auto j = Fei::Http::FReflect::fromClass(cate);
  j["result"] = ApiOk;
  res.setBody(JsonTool::ToString(j));
  return res;
}

} // namespace Blog