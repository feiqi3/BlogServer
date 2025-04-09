#include "AdminLoginController.h"
#include "Core/JsonTool.h"
#include "FConfigReader.h"
#include "FLogger.h"
#include "Http/FController.h"
#include "Http/FCookie.h"
#include "Http/FHttpResponse.h"
#include <chrono>
#include <string>

#include "Core/ApiChangeDataDef.h"
#include "Core/Session.h"
#include "Service/AdminLogin.h"
#include "Service/QuickRedirect.h"
#include "nlohmann/json_fwd.hpp"

#define MODULE_NAME "[AdminLoginController]"
namespace {
const int sMaxErrorTime = 5;
nlohmann::json getErrorJson(const std::string &msg) {
  nlohmann::json j;
  j["result"] = Blog::ApiError;
  j["msg"] = msg;
  return j;
}
nlohmann::json getSucc() {
  nlohmann::json j;
  j["result"] = Blog::ApiOk;
  return j;
}

} // namespace

namespace Blog {

AdminController::AdminController() : Fei::Http::FControllerBase("AdminLogin") {}

void AdminController::lateInit() {
  auto cfg = Fei::FConfigReader::instance();
  auto user = cfg->getCfg("AdminUser");
  this->mUserName = user.value_or("admin");
  auto password = cfg->getCfg("admin");
  if (user->empty() || password->empty()) {
    Fei::Logger::instance()->log(
        Fei::lvl::warn,
        MODULE_NAME "Admin user or password not set. Check config file.");
  }
}

Fei::Http::FHttpResponse
AdminController::Login(const Fei::Http::FHttpRequest &req,
                       const Fei::Http::FPathVar &var) {
  auto admin = AdminLogin::instance();
  bool isLogin = true;
  Fei::Http::FHttpResponse res;

  if (admin->isOnLock()) {
    isLogin = false;
  }

  nlohmann::json json;
  if (isLogin) {
    json = JsonTool::ToJson(req.getRequestBody());
  }

  if (json.is_null()) {
    isLogin = false;
  }

  std::string sessionId;
  if (isLogin) {
    std::string username, userpswd;
    username = json["username"].template get<std::string>();
    userpswd = json["userpassword"].template get<std::string>();
    isLogin = admin->Login(username, userpswd, sessionId);
  }
  nlohmann::json j;

  if (isLogin) {
    Fei::Http::FCookie cookie;
    cookie.addValue("sessionId", sessionId);
    cookie.addAttribute("Path", "/");

    cookie.addAttribute(
        "Max-Age",
        std::to_string(SessionManager::instance()->getSessionExpireTimeMins()));
    cookie.addAttribute("HttpOnly");
#ifndef FEI_DEBUG
    cookie.addAttribute("SameSite", "Strict");
    cookie.addAttribute("Secure");
#endif
    res.addCookie(cookie);
    res.addHeader("Access-Control-Allow-Credentials", "true");
    j["result"] = ApiOk;
  } else {
    j["result"] = ApiError;
    j["msg"] = "Wrong username or psw";
  }
  res.setBody(JsonTool::ToString(j));
  return res;
}

Fei::Http::FHttpResponse
AdminController::Post(const Fei::Http::FHttpRequest &req,
                      const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  nlohmann::json json = JsonTool::ToJson(req.getRequestBody());
  if (!AdminLogin::instance()->isLogin(req)) {
    nlohmann::json j = getErrorJson("Not login");
    res.setBody(JsonTool::ToString(j));
    return res;
  }
  std::string outErr;
  bool result = AdminLogin::instance()->postOrModifyBlog(json, outErr);
  if (!result) {
    Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME "Post error: {}",
                                 outErr);
    res.setBody(JsonTool::ToString(getErrorJson(outErr)));
    return res;
  } else {
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  }
}

Fei::Http::FHttpResponse
AdminController::Delete(const Fei::Http::FHttpRequest &req,
                        const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  nlohmann::json json = JsonTool::ToJson(req.getRequestBody());
  if (!AdminLogin::instance()->isLogin(req)) {
    nlohmann::json j = getErrorJson("Not login");
    res.setBody(JsonTool::ToString(j));
    return res;
  }

  auto itorId = json.find("id");
  if (itorId == json.end()) {
    res.setBody(JsonTool::ToString(getErrorJson("json msg error!")));
    return res;
  }
  uint64_t id = itorId->get<uint64_t>();
  std::string outErr;
  bool result = AdminLogin::instance()->deleteBlog(id, outErr);
  if (!result) {
    Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME "Post Delete error: {}",
                                 outErr);
    res.setBody(JsonTool::ToString(getErrorJson(outErr)));
    return res;
  } else {
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  }
}

} // namespace Blog