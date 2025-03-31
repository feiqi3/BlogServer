#include "Http/FCookie.h"
#include <sstream>
#include <string>

namespace Fei::Http {

bool FCookie::getValue(const std::string &key, std::string &outVal) const {

  auto itor = mValueMap.find(key);
  if (itor == mValueMap.end()) {
    return false;
  }

  outVal = itor->second;
  return true;
}
void FCookie::addValue(const std::string& key, const std::string& val) {
  mValueMap[key] = val;
}

// void FCookie::setExpires(); // TODO:
// void FCookie::getExpires(); // TODO:

void FCookie::addAttribute(const std::string &attr) { mMap[attr] = ""; }
void FCookie::addAttribute(const std::string &attr,const std::string& val) { mMap[attr] = val; }

bool FCookie::hasAttribute(const std::string &attr) const {
  auto itor = mMap.find(attr);
  if (itor == mMap.end()) {
    return false;
  }
  return true;
}

bool FCookie::hasAttribute(const std::string &attr, std::string &val) const {
  auto itor = mMap.find(attr);
  if (itor == mMap.end()) {
    return false;
  }
  val = itor->second;
  return true;
}

std::string FCookie::outSetCookieNoHeader() const {
  std::stringstream ssOut;
  // SetCookie header.
  auto size = mMap.size();
  for (auto &&[key, val] : mMap) {
    ssOut << key;
    if (val.size() != 0) {
      ssOut << "=" << val;
    }
    if (--size != 0) {
      ssOut << ";";
    }
  }
  return ssOut.str();
}
std::string FCookie::outSetCookie() const {
  std::stringstream ssOut;
  // SetCookie header.
  ssOut << "Set-Cookie: ";
  auto size = mMap.size() + mValueMap.size();
  //RFC 6265
  for (auto &&[key, val] : mValueMap) {
    ssOut << key;
    if (val.size() != 0) {
      ssOut << "=" << val;
    }
    if (--size != 0) {
      ssOut << ";";
    }
  }
  for (auto &&[key, val] : mMap) {
    ssOut << key;
    if (val.size() != 0) {
      ssOut << "=" << val;
    }
    if (--size != 0) {
      ssOut << ";";
    }
  }
    auto&& retRes = ssOut.str();
    return retRes;
}

} // namespace Fei::Http
