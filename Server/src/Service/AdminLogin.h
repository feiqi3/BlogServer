#pragma once
#include "Utils/Singleton.h"
#include "Http/FHttpRequest.h"
#include <atomic>
#include <chrono>
#include <string>

namespace Blog{
    class AdminLogin : public Singleton<AdminLogin>{
        public:
        AdminLogin();
        bool Login(const std::string& userName,const std::string& password,std::string& sessionId);
        bool isLogin(const Fei::Http::FHttpRequest& req)const;
        bool isLogin(const std::string& sessionId)const;
        bool isOnLock();
        
        private:
        std::string mUserName;
        std::string mPassword;
        std::atomic<bool> mIsLocked = false;
        std::atomic<uint32_t> mFailedCount = 0;
        std::chrono::system_clock::time_point mLockTime;
        std::chrono::system_clock::time_point mLastLoginTime;
    };
};