#pragma once
#include "DAO/ORM.h"
#include "ModelDef.h"
#include <cstdint>
#include <string>

namespace Blog {
class TestMessage {
public:
  uint64_t id;
  std::string name;
  std::string content;
  uint64_t created_at ;
  ENTITY_TABLE(TestMessage)
  AUTO_INC_PK(id)
};
}; // namespace Blog