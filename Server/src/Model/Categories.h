#pragma once
#include "ModelDef.h"

namespace Blog::Model {
class Category{
public:
 uint64_t id;
 std::string name;
 std::string categorypic;
 ENTITY_TABLE(Categories);
};
}