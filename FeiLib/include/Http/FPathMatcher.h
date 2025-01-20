#ifndef FPATHMATCHER_H
#define FPATHMATCHER_H

#include "FHttpDef.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace re2 {
    class RE2;
};

namespace Fei::Http {

//-----------------------------------------------------------------//
    using PathVarMap = std::map<std::string, std::string>;
//-----------------------------------------------------------------//

//An ANT-Style path matcher
class FPathMatcher {
public:
  FPathMatcher(const std::string& pattern) : FPathMatcher(pattern, true) {}
  FPathMatcher(const std::string& pattern,bool caseSensitive);
  bool isMatch(const std::string& str, PathVarMap& vars);
private:
    std::vector<std::string> varNames;
    std::unique_ptr<re2::RE2> mPattern;
};

}; // namespace Fei::Http

#endif