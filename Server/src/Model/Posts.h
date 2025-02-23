#pragma once
#include "ModelDef.h"
#include <cstdint>

namespace Blog::Model {
class Post {
public:
  uint64_t id;
  uint64_t user_id;
  std::string title;
  std::string content;
  std::string profile;
  uint64_t category_id;
  int status;
  uint64_t created_at;
  uint64_t updated_at;
  uint64_t view_times;
};
} // namespace Blog::Model