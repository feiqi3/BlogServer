#pragma once
#include "Model/Users.h"
#include <string>

namespace Blog::DAO{
    class UserQuery{
        public:
        static bool queryUserAndPasswordHash(const std::string& userName,const std::string& hash);
    };
};