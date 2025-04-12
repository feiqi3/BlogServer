#include "DBTestController.h"
#include "Core/ApiChangeDataDef.h"
#include "Core/JsonTool.h"
#include "Core/ServerBasicDef.h"
#include "DAO/TestMessageQuery.h"
#include "Http/FHttpResponse.h"
#include "Utils/Digital.h"
#include "Utils/FileReader.h"
#include <exception>
namespace Blog {
Fei::Http::FHttpResponse
DatabaseTestPageController::PostMessage(const Fei::Http::FHttpRequest &req,
                                        const Fei::Http::FPathVar &) {
  Fei::Http::FHttpResponse res;
  if (req.getRequestBody().size() > 2048) {
    res.setBody(JsonTool::ToString(getErrorJson("Request body too long!")));
    return res;
  }
  auto j = JsonTool::ToJson(req.getRequestBody());
  if (j.is_null()) {
    res.setBody(JsonTool::ToString(getErrorJson("Json Error!")));
    return res;
  }
  TestMessage message;
  try {
    message.name = j["name"].template get<std::string>();
    message.content = j["content"].template get<std::string>();
    message.created_at =
        std::chrono::system_clock::now().time_since_epoch().count();
    auto ret = DAO::TestMessageQuery::InsertMessage(message);
    if (ret.has_value()) {
      res.setBody(JsonTool::ToString(getErrorJson(ret.value())));
      return res;
    }
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  } catch (std::exception e) {
    res.setBody(JsonTool::ToString(getErrorJson("Json Error!")));
    return res;
  }
}

Fei::Http::FHttpResponse
DatabaseTestPageController::GetMessageByPage(const Fei::Http::FHttpRequest &,
                                             const Fei::Http::FPathVar &var) {
  auto page = var.get("page");
  int tarPageNum = 0;
  if (Digital::isNumber(page)) {
    tarPageNum = std::stoi(page);
  }
  if (tarPageNum < 0) {
    tarPageNum = 0;
  }
  const int perpageNum = 20;
  int pageNum = DAO::TestMessageQuery::QueryMessagePageNum() / perpageNum;
  tarPageNum = std::min(pageNum, tarPageNum);

  auto messages =
      DAO::TestMessageQuery::QueryMessageByPage(tarPageNum, perpageNum);
  nlohmann::json j;
  j["messages"] = nlohmann::json::array();
  for (auto &message : messages) {
    nlohmann::json m;
    m["name"] = std::get<0>(message);
    m["content"] = std::get<1>(message);
    j["messages"].push_back(m);
  }
  j["pagenum"] = pageNum;
  Fei::Http::FHttpResponse res;
  res.setBody(JsonTool::ToString(j));
  return res;
}
Fei::Http::FHttpResponse
DatabaseTestPageController::GetMessagePage(const Fei::Http::FHttpRequest &,
                                           const Fei::Http::FPathVar &) {
  MemoryMappedFile file(BlogWebPagePath + "testMessage.html", Mode::ReadOnly,
                        0);
  Fei::Http::FHttpResponse res;
  res.setBody(std::string((char *)file.data(), file.size()));
  return res;
}
} // namespace Blog