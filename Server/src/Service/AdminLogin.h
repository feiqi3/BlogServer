#pragma once
#include "Utils/Singleton.h"
#include "Http/FHttpRequest.h"
#include <atomic>
#include <chrono>
#include <string>
#include "Core/JsonTool.h"

namespace Blog{
    class AdminLogin : public Singleton<AdminLogin>{
        public:
        AdminLogin();
        bool Login(const std::string& userName,const std::string& password,std::string& sessionId);
        bool isLogin(const Fei::Http::FHttpRequest& req)const;
        bool isLogin(const std::string& sessionId)const;
        bool isOnLock();
        
        bool postOrModifyBlog(const nlohmann::json& json,std::string& out);
        bool deleteBlog(uint64_t id,std::string& out);
        bool postOrModifyCategory(const nlohmann::json& json,std::string& out);
        bool deleteCategory(uint64_t id,std::string& out);
        private:

        std::string mUserName;
        std::string mPassword;
        std::atomic<bool> mIsLocked = false;
        std::atomic<uint32_t> mFailedCount = 0;
        std::chrono::system_clock::time_point mLockTime;
        std::chrono::system_clock::time_point mLastLoginTime;
    };
};