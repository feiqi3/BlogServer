#pragma once
#include "ModelDef.h"
#include <cstdint>
#include <string>

namespace Blog::Model {
class Photo {
public:
  uint64_t id;
  uint64_t album_id;
  std::string url;
  std::string caption;
  int sort_order;
  uint64_t created_at;
  std::string title;
  std::string description;
  uint64_t view_times;
  int width = 0;
  int height = 0;
  ENTITY_TABLE(Photos);
  AUTO_INC_PK(id);
};
} // namespace Blog::Model
