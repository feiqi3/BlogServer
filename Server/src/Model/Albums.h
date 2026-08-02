#pragma once
#include "ModelDef.h"
#include <cstdint>
#include <string>

namespace Blog::Model {
class Album {
public:
  uint64_t id;
  std::string name;
  std::string description;
  std::string cover_url;
  int sort_order;
  uint64_t created_at;
  ENTITY_TABLE(Albums);
  AUTO_INC_PK(id);
};
} // namespace Blog::Model
