#pragma once

#include "Utils/Singleton.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace Blog{
    class SessionManagerPrivate;
    class SessionManager :public Singleton<SessionManager>{
        public:
        SessionManager();
        ~SessionManager();

        void checkOverdue(uint64_t time_ms);
        
        std::string addSession();
        
        bool hasSession(const std::string& sessionId);
        bool addDataToSession(const std::string& sessionId,const std::string& key,const std::string& data);
        bool getDataFromSession(const std::string& sessionId, const std::string& key, std::string& out);

        void deleteSession(const std::string& sessionId);
        
        uint32_t getSessionExpireTimeMins()const ;

        private:

        SessionManagerPrivate* mDp = 0;

    };
};