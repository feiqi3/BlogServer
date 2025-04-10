#pragma once
#include "AdminLogin.h"
#include "DAO/ORM.h"
#include "FLogger.h"
#include "FConfigReader.h"
#include "Core/Session.h"
#include "Utils/Digital.h"
#include "Utils/TimeHelper.h"
#include "Core/JsonTool.h"
#include "DAO/QueryPosts.h"
#include "Model/Posts.h"
#include <cstdint>
#include <string>
#define MODULE_NAME "[AdminLogin]"

namespace {
    const int sMaxErrorTime = 5;
}

namespace Blog{
    AdminLogin::AdminLogin(){
        auto cfg = Fei::FConfigReader::instance();
        auto user = cfg->getCfg("AdminUser");
        this->mUserName = user.value_or("admin");
        auto password = cfg->getCfg("admin");
        this->mPassword = password.value_or("admin");
        if(user->empty() || password->empty()){
            Fei::Logger::instance()->log(Fei::lvl::warn,MODULE_NAME "Admin user or password not set. Check config file.");
        }
    }

    bool AdminLogin::isOnLock(){
        if(mIsLocked){
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - mLockTime);
            if(duration.count() > 60){
                mIsLocked = false;
                mFailedCount = 0;
                return false;
            } 

            return true;
        }
        return false;

    };

    bool AdminLogin::isLogin(const Fei::Http::FHttpRequest& req)const{
        auto cookieNum = req.getCookieSize();
        std::string sessionId;
        for(auto i = 0; i < cookieNum; i++){
            auto& cookie = req.getCookie(i);
            if(cookie.getValue("sessionId", sessionId))
            {
                break;
            }
        }
        if(!sessionId.empty()){
            bool islogin = this->isLogin(sessionId);
            return islogin;
        } 
        return false;
    }


    bool AdminLogin::isLogin(const std::string& sessionId)const{
        if(SessionManager::instance()->hasSession(sessionId)){
            return true;
        }
        return false;
    }

    bool AdminLogin::postOrModifyBlog(const nlohmann::json& json,std::string& out){
        bool isModify = false;
        
        auto titleJson = json["title"];
        if(titleJson.is_null()){
            out = "title is null";
            return false;
        }
        std::string title = titleJson.get<std::string>();
        if(title.empty()){
            out = "title is empty";
            return false;
        }

        auto profileJson = json["profile"];
        if(profileJson.is_null()){
            out = "profile is null";
            return false;
        }
        std::string profile = profileJson.get<std::string>();
        if(profile.empty()){
            out = "profile is empty";
            return false;
        }

        auto contentJson = json["content"];
        if(contentJson.is_null()){
            out = "content is null";
            return false;
        }
        std::string content = contentJson.get<std::string>();
        if(content.empty()){
            out = "content is empty";
            return false;
        }

        auto titlepicJson = json["titlepic"];
        if(contentJson.is_null()){
            out = "titlepic is null";
            return false;
        }
        std::string titlepic = titlepicJson.get<std::string>();
        if(titlepic.empty()){
            out = "titlepic is empty";
            return false;
        }
        Model::Post post;
        //modyify  
        auto idItor = json.find("id");
        if(idItor != json.end()){
            auto idstr = idItor->get<std::string>();
            if(!Digital::isNumber(idstr)){
                out = "id is wrong";
                return false;
            }
            uint64_t id  = std::stoull(idstr);
            auto postopt = DAO::PostQuery::QueryPostById(id);
            if(!postopt.has_value()){
                out = "post not exist";
                return false;
            }
            post.id = id;
            post.title = title;
            post.profile = profile;
            post.content = content;
            post.titlepic = titlepic;
            post.updated_at = TimeHelper::getCurrentTimeFromEpochMills();
            //TODO: modify this
            post.category_id = 0;
            auto out = DAO::PostQuery::UpdatePostById(id, post);
            if(out.has_value()){
                out = out.value();
                return false;
            }
            return true;
        }
        else{
            //insert
            Model::Post post;
            post.title = title;
            post.profile = profile;
            post.content = content;
            post.titlepic = titlepic;
            post.created_at = TimeHelper::getCurrentTimeFromEpochMills();
            post.updated_at = 0;
            //TODO: modify this
            post.category_id = 0;
            auto res = DAO::PostQuery::InsertPost(post);
            if(res.has_value()){
                out = res.value();
                return false;
            }
            return true;
        }

    }

    bool AdminLogin::Login(const std::string& userName,const std::string& password,std::string& sessionId){
        //some more complicated condition?
        if(!sessionId.empty() && SessionManager::instance()->hasSession(sessionId)){
            return true;
        }        
        
        bool isLoginSuccess = true;

        if(mIsLocked)return false;

        if(userName == mUserName && password == mPassword){
            isLoginSuccess = true;
            mFailedCount = 0;
            sessionId = SessionManager::instance()->addSession();
        }else{
            isLoginSuccess = false;
            mFailedCount ++;
        }

        if(mFailedCount >= sMaxErrorTime){
            mIsLocked = true;
            mLockTime=  std::chrono::system_clock::now();
            mFailedCount = 0;
        }

        return isLoginSuccess;
    }

    bool AdminLogin::deleteBlog(uint64_t id,std::string& out){
        auto res = DAO::PostQuery::DeletePostById(id);
        if(res.has_value()){
            out = res.value();
            return false;
        }
        return true;
    }


};