#pragma once
#include "AdminLogin.h"
#include "Core/JsonTool.h"
#include "Core/Session.h"
#include "DAO/CategoryQuery.h"
#include "DAO/ORM.h"
#include "DAO/QueryPosts.h"
#include "FConfigReader.h"
#include "FLogger.h"
#include "Model/Categories.h"
#include "Model/Posts.h"
#include "Utils/Digital.h"
#include "Utils/FileReader.h"
#include "Utils/TimeHelper.h"
#include "Utils/HtmlHelper.h"
#include <cstdint>
#include <string>
#define MODULE_NAME "[AdminLogin]"

namespace {
const int sMaxErrorTime = 5;
}

namespace Blog {
AdminLogin::AdminLogin() {
  auto cfg = Fei::FConfigReader::instance();
  auto user = cfg->getCfg("AdminUser");
  this->mUserName = user.value_or("admin");
  auto password = cfg->getCfg("AdminPassword");
  this->mPassword = password.value_or("admin");
  if (!user.has_value() || !password.has_value()|| user->empty() || password->empty()) {
    Fei::Logger::instance()->log(
        Fei::lvl::warn,
        MODULE_NAME "Admin user or password not set. Check config file.");
  }
}

bool AdminLogin::isOnLock() {
  if (mIsLocked) {
    auto now = std::chrono::system_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::minutes>(now - mLockTime);
    if (duration.count() > 60) {
      mIsLocked = false;
      mFailedCount = 0;
      return false;
    }

    return true;
  }
  return false;
};

bool AdminLogin::isLogin(const Fei::Http::FHttpRequest &req) const {
  auto cookieNum = req.getCookieSize();
  std::string sessionId;
  for (auto i = 0; i < cookieNum; i++) {
    auto &cookie = req.getCookie(i);
    if (cookie.getValue("sessionId", sessionId)) {
      break;
    }
  }
  if (!sessionId.empty()) {
    bool islogin = this->isLogin(sessionId);
    return islogin;
  }
  return false;
}

bool AdminLogin::isLogin(const std::string &sessionId) const {
  if (SessionManager::instance()->hasSession(sessionId)) {
    return true;
  }
  return false;
}


bool AdminLogin::postOrModifyBlog(const nlohmann::json &json,
                                  std::string &out) {
  bool isModify = false;
  uint32_t categoryId = 0;
  bool isvalid = true;
  {
      std::string categoryStr;
      isvalid = JsonTool::get(json, std::string("category_id"), std::string(""), categoryStr);
      if (!isvalid) {
        out = "Missing parameter: category_id";
        return false;
      }
      if (Digital::isNumber(categoryStr)) {
          categoryId = std::stoul(categoryStr);
      }
  }

  {
    
  }
  std::string title;
  {
      isvalid = JsonTool::get(json, std::string("title"), std::string(), title);
      if (!isvalid) {
          out = "Missing parameter: title";
          return false;
      }
      if (title.empty()) {
          out = "title is empty";
          return false;
      }
  }

  std::string profile;
  {
      isvalid = JsonTool::get(json, std::string("profile"), std::string(), profile);
      if (!isvalid) {
          out = "Missing parameter: profile";
          return false;
      }
      if (profile.empty()) {
          out = "profile is empty";
          return false;
      }
  }

  std::string content;
  {
      isvalid = JsonTool::get(json, std::string("content"), std::string(), content);
      if (!isvalid) {
          out = "Missing parameter: content";
          return false;
      }
      if (content.empty()) {
          out = "content is empty";
          return false;
      }
  }

  content = html_escape(content);

  std::string titlepic;
  {
      isvalid = JsonTool::get(json, std::string("titlepic"), std::string(), titlepic);
      if (!isvalid) {
          out = "Missing parameter: titlepic";
          return false;
      }
      if (titlepic.empty()) {
          out = "titlepic is empty";
          return false;
      }
  }

  Model::Post post;
  // modyify
  auto idItor = json.find("id");
  if (idItor != json.end()) {
    auto idstr = idItor->get<std::string>();
    if (!Digital::isNumber(idstr)) {
      out = "id is wrong";
      return false;
    }
    uint64_t id = std::stoull(idstr);
    auto postopt = DAO::PostQuery::QueryPostById(id);
    if (!postopt.has_value()) {
      out = "post not exist";
      return false;
    }
    post.id = id;
    post.title = title;
    post.profile = profile;
    post.content = content;
    post.titlepic = titlepic;
    post.updated_at = TimeHelper::getCurrentTimeFromEpochMills();
    post.category_id = categoryId;
    auto out = DAO::PostQuery::UpdatePostById(id, post);
    if (out.has_value()) {
      out = out.value();
      return false;
    }
    return true;
  } else {
    // insert
    Model::Post post;
    post.view_times = 0;
    post.title = title;
    post.profile = profile;
    post.content = content;
    post.titlepic = titlepic;
    post.created_at = TimeHelper::getCurrentTimeFromEpochMills();
    post.updated_at = 0;
    post.category_id = categoryId;
    auto res = DAO::PostQuery::InsertPost(post);
    if (res.has_value()) {
      out = res.value();
      return false;
    }
    return true;
  }
}

bool AdminLogin::Login(const std::string &userName, const std::string &password,
                       std::string &sessionId) {
  // some more complicated condition?
  if (!sessionId.empty() && SessionManager::instance()->hasSession(sessionId)) {
    return true;
  }

  bool isLoginSuccess = true;

  if (mIsLocked)
    return false;

  if (userName == mUserName && password == mPassword) {
    isLoginSuccess = true;
    mFailedCount = 0;
    sessionId = SessionManager::instance()->addSession();
  } else {
    isLoginSuccess = false;
    mFailedCount++;
  }

  if (mFailedCount >= sMaxErrorTime) {
    mIsLocked = true;
    mLockTime = std::chrono::system_clock::now();
    mFailedCount = 0;
  }

  return isLoginSuccess;
}

bool AdminLogin::deleteBlog(uint64_t id, std::string &out) {
  auto res = DAO::PostQuery::DeletePostById(id);
  if (res.has_value()) {
    out = res.value();
    return false;
  }
  return true;
}

bool AdminLogin::postOrModifyCategory(const nlohmann::json &json,
                                      std::string &out) {
  auto iditor = json.find("id");
  bool isModify = false;
  uint64_t id;
  if (iditor != json.end()) {
    isModify = true;
    auto tempStr = iditor->get<std::string>();
    bool isvalid = Digital::isNumber(tempStr);
    if (!isvalid) {
      out = "Wrong id";
      return false;
    }
    id = std::stoull(tempStr);
  }

  auto name = json["name"].template get<std::string>();
  auto pic = json["categorypic"].template get<std::string>();
  if (name.empty()) {
    out = "No name!";
    return false;
  }
  if (pic.empty()) {
    auto cfg = Fei::FConfigReader::instance()->getCfg("defaultPic");
    if (cfg.has_value()) {
      pic = cfg.value();
    } else {
      Fei::Logger::instance()->log(Fei::lvl::err, MODULE_NAME
                                   "Doesnt set default picture in config.");
      out = "Internal error";
      return false;
    }
  }

  Model::Category cate{.name = name, .categorypic = pic};
  if (isModify) {
    cate.id = id;
    auto ret = DAO::CategoryQuery::UpdateCategory(cate);
    if (ret.has_value()) {
      out = ret.value();
      return false;
    }
    return true;
  } else {
    auto ret = DAO::CategoryQuery::InsertCategory(cate);
    if (ret.has_value()) {
      out = ret.value();
      return false;
    }
    return true;
  }
}

bool AdminLogin::deleteCategory(uint64_t id, std::string &out) {
  auto ret = DAO::CategoryQuery::DeleteCategory(id);
  if (ret.has_value()) {
    out = ret.value();
    return false;
  }
  return true;
}

}; // namespace Blog