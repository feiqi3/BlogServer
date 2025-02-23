#pragma once
#include "ModelDef.h"

namespace Blog::Model {

class User{
public:
uint64_t id;
std::string username;
std::string email;
std::string password_hash;
ENTITY_TABLE(Users)
};

}