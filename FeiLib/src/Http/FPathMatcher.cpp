#include "Http/FPathMatcher.h"

// DOOOO Not Use std::regex!!!
#include "re2/re2.h"

//using namespace re2;
namespace Fei::Http {

RE2 VaribleMatchPattern = "?|*|{((?:{[^/]+?}|[^/{}]|[{}])+?)}";

FPathMatcher::FPathMatcher(std::string pattern) {
}
}; // namespace Fei::Http
