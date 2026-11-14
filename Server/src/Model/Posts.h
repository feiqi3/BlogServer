#pragma once
#include "ModelDef.h"
#include <cstdint>

namespace Blog::Model {
class Post {
public:
  uint64_t id;
  uint64_t user_id;
  std::string title;
  uint64_t category_id;
  std::string profile;
  std::string titlepic;
  std::string content;
  std::string tags;
  int status;
  uint64_t created_at;
  uint64_t updated_at;
  uint64_t view_times;
  int allow_comment;
  int allow_rss;
  int hide;
  std::string script_name;
 ENTITY_TABLE(Posts);
 AUTO_INC_PK(id);
};
} // namespace Blog::Model