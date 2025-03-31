#ifndef FCOOKIE_H
#define FCOOKIE_H

#include "FDef.h"
#include <map>
#include <string>

namespace Fei::Http {
using CookiesMap = std::map<std::string, std::string>;

class F_API FCookie {
public:
  FCookie() {}

  FCookie &operator=(const FCookie &rhs) {
    mMap = rhs.mMap;
    mValueMap = rhs.mValueMap;
    return *this;
  }

  FCookie &operator=(FCookie &&rhs) {
    mMap = std::move(rhs.mMap);
    mValueMap = std::move(rhs.mValueMap);
    return *this;
  }
  FCookie(FCookie &&rhs) {
    mMap = std::move(rhs.mMap);
    mValueMap = std::move(rhs.mValueMap);
  }

  FCookie(const FCookie &rhs) {
    mMap = (rhs.mMap);
    mValueMap = rhs.mValueMap;
  }
  bool getValue(const std::string &key, std::string &outVal) const;
  void addValue(const std::string &key, const std::string &val);

  // void setExpires();//TODO:
  // void getExpires();//TODO:

  void addAttribute(const std::string &attr);
  void addAttribute(const std::string &attr, const std::string &val);
  bool hasAttribute(const std::string &attr) const;
  bool hasAttribute(const std::string &attr, std::string &val) const;

  std::string outSetCookieNoHeader() const;
  std::string outSetCookie() const;

  bool empty() const { return mMap.empty(); }

private:
  CookiesMap mValueMap;
  CookiesMap mMap;
};

} // namespace Fei::Http

#endif