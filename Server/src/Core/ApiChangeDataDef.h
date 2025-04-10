#ifndef APICHANGEDATADEF_H_
#define APICHANGEDATADEF_H_
#include "nlohmann/json.hpp"

namespace Blog {
const int ApiOk = 200;
const int ApiError = 500;
const int ApiNotFound = 400;

inline nlohmann::json getErrorJson(const std::string &msg) {
  nlohmann::json j;
  j["result"] = Blog::ApiError;
  j["msg"] = msg;
  return j;
}
inline nlohmann::json getSucc() {
  nlohmann::json j;
  j["result"] = Blog::ApiOk;
  return j;
}

}; // namespace Blog

#endif