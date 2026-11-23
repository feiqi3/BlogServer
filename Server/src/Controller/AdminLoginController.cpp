#include "AdminLoginController.h"
#include "Core/JsonTool.h"
#include "FConfigReader.h"
#include "FLogger.h"
#include "Http/FController.h"
#include "Http/FCookie.h"
#include "Http/FHttpResponse.h"
#include <cctype>

#include <string>

#include "Core/ApiChangeDataDef.h"
#include "Core/Session.h"
#include "Service/AdminLogin.h"
#include "Service/QuickRedirect.h"
#include "Service/ProfilerService.h"
#include "nlohmann/json_fwd.hpp"
#include "Utils/Digital.h"
#ifdef FEILIB_ENABLE_SCRIPT
#include "Service/ArticleHookService.h"
#endif
#define MODULE_NAME "[AdminLoginController]"
namespace {
const int sMaxErrorTime = 5;
} // namespace

namespace Blog {

AdminController::AdminController() : Fei::Http::FControllerBase("AdminLogin") {}

void AdminController::lateInit() {
}

Fei::Http::FHttpResponse AdminController::Profiling(const Fei::Http::FHttpRequest& req,const Fei::Http::FPathVar&){
    Fei::Http::FHttpResponse response;
    response.setContentType("application/json").addHeader("Cache-Control","no-store");
    if(!AdminLogin::instance()->isLogin(req)){
        response.setStatusCode(Fei::Http::StatusCode::_401).setBody("{\"error\":\"Login required\"}");return response;
    }
    auto profiler=ProfilerService::current();
    response.setBody((profiler?profiler->status():nlohmann::json({{"enabled",false}})).dump());return response;
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
    JsonTool::get(json, std::string("username"), std::string(""), username);
    JsonTool::get(json, std::string("userpassword"), std::string(""), userpswd);
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
  uint64_t savedId=0;
#ifdef FEILIB_ENABLE_SCRIPT
  auto hooks=Script::ArticleHookService::current();
  if(Script::ArticleHookService::stopping()){
    res.setStatusCode(Fei::Http::StatusCode::_503).setContentType("application/json")
      .setBody(JsonTool::ToString(getErrorJson("Server is stopping")));
    return res;
  }
  if(hooks){if(auto response=hooks->submit(json))return std::move(*response);}
#endif
  bool result = AdminLogin::instance()->postOrModifyBlog(json, outErr,&savedId);
  if (!result) {
    Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME "Post error: {}",
                                 outErr);
    res.setBody(JsonTool::ToString(getErrorJson(outErr)));
    return res;
  } else {
#ifdef FEILIB_ENABLE_SCRIPT
    if(hooks)hooks->saved(savedId,json);
#endif
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  }
}

Fei::Http::FHttpResponse
AdminController::DeletePost(const Fei::Http::FHttpRequest &req,
                        const Fei::Http::FPathVar &var) {
  Fei::Http::FHttpResponse res;
  if (!AdminLogin::instance()->isLogin(req)) {
    nlohmann::json j = getErrorJson("Not login");
    res.setBody(JsonTool::ToString(j));
    return res;
  }
  auto idStr = var.get("id");
  if(idStr.empty()){
    res.setBody(JsonTool::ToString(getErrorJson("No id error!")));
    return res;
  }
  if(!Digital::isNumber(idStr)){
    res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
    return res;
  }
  
  uint64_t id = std::stoul(idStr);
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

Fei::Http::FHttpResponse AdminController::Category(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
  Fei::Http::FHttpResponse res;
  nlohmann::json json = JsonTool::ToJson(req.getRequestBody());
  if (!AdminLogin::instance()->isLogin(req)) {
    nlohmann::json j = getErrorJson("Not login");
    res.setBody(JsonTool::ToString(j));
    return res;
  }
  std::string outErr;
  bool result = AdminLogin::instance()->postOrModifyCategory(json, outErr);
  if (!result) {
    Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME "Category error: {}",
                                 outErr);
    res.setBody(JsonTool::ToString(getErrorJson(outErr)));
    return res;
  } else {
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  }
}

Fei::Http::FHttpResponse AdminController::DeleteCategroy(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var){
  Fei::Http::FHttpResponse res;
  auto idStr = var.get("id");
  if(idStr.empty()){
    res.setBody(JsonTool::ToString(getErrorJson("No id error!")));
    return res;
  }
  if(!Digital::isNumber(idStr)){
    res.setBody(JsonTool::ToString(getErrorJson("Id Error!")));
    return res;
  }
  
  uint64_t id = std::stoul(idStr);
  std::string outErr;
  bool result = AdminLogin::instance()->deleteCategory(id, outErr);
  if (!result) {
    Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME "Category Delete error: {}",
                                 outErr);
    res.setBody(JsonTool::ToString(getErrorJson(outErr)));
    return res;
  } else {
    res.setBody(JsonTool::ToString(getSucc()));
    return res;
  }
}
} // namespace Blog
