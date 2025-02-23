#pragma once
#include "ModelDef.h"

namespace Blog::Model {

class Tag{
public:
uint64_t id;
std::string name;
ENTITY_TABLE(Tags)
};

}