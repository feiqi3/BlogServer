#ifndef FPATHMATCHER_H
#define FPATHMATCHER_H

#include "FHttpDef.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace Fei::Http {

//-----------------------------------------------------------------//

class FMatchResult {

  void set(const std::string &varName, const std::string &varValue) {
    mPathVarMap[varName] = varValue;
  }

  std::string get(const std::string &varName) const {
    auto itor = mPathVarMap.find(varName);
    if (itor == mPathVarMap.end()) {
      return {};
    }
    return itor->second;
  }

  void setIserData(void *data) { userData = data; }
  void *getUserData() { return userData; }

private:
  void *userData = 0;
  std::unordered_map<std::string, std::string> mPathVarMap;
};
//-----------------------------------------------------------------//

//An ANT-Style path matcher
class FPathMatcher {
public:
  FPathMatcher(std::string pattern);
  FPathMatcher(std::string pattern,bool caseSensitive);
};

}; // namespace Fei::Http

#endif