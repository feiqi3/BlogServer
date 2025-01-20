#ifndef FPATHMATCHER_H
#define FPATHMATCHER_H

#include "FHttpDef.h"
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace re2 {
    class RE2;
};

namespace Fei::Http {

//-----------------------------------------------------------------//
    using PathVarMap = std::map<std::string, std::string>;
//-----------------------------------------------------------------//
    static const uint32 MaxPathLengthMatcherSupport = 1024;
//An ANT-Style path matcher
class FPathMatcher {
public:
  FPathMatcher(const std::string& pattern) : FPathMatcher(pattern, true) {}
  FPathMatcher(const std::string& pattern,bool caseSensitive);
  bool isMatch(const std::string& str, PathVarMap& vars);
  uint32 getWildCardsNums()const { return mWildCardsNums; }
  uint32 getVariableNums()const { return varNames.size(); }
  uint32 getUndecidedCharNums()const { return mUndecidedChars; }
  const std::vector<std::string>& getVarbiables()const { return varNames; }
  const std::string& getOriginPattern()const { return mOriginPattern; }
  ~FPathMatcher();
private:
    uint32 mWildCardsNums = 0;
    uint32 mUndecidedChars = 0;
    std::vector<std::string> varNames;
    std::unique_ptr<re2::RE2> mPattern;
    std::string mOriginPattern;
};

}; // namespace Fei::Http

#endif