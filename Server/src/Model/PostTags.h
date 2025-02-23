#pragma once
#include "DAO/ORM.h"
#include "ModelDef.h"

namespace Blog::Model {

class PostTag{
public:
uint64_t post_id;
uint64_t tag_id;
ENTITY_TABLE(PostTags)
};

}