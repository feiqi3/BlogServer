#pragma once
#include "AdminLogin.h"
#include "FLogger.h"
#include "FConfigReader.h"
#include "Core/Session.h"
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

};