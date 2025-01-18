#ifndef FCOOKIE_H
#define FCOOKIE_H

#include <map>
#include <string>

namespace Fei::Http {
using CookiesMap = std::map<std::string, std::string>;

class FCookie {
public:
  FCookie() {}

  FCookie &operator=(const FCookie &rhs) {
    mMap = rhs.mMap;
    return *this;
  }

  FCookie &operator=(FCookie &&rhs) {
    mMap = std::move(rhs.mMap);
    return *this;
  }
  FCookie(FCookie &&rhs) { mMap = std::move(rhs.mMap); }

  FCookie(const FCookie &rhs) { mMap = (rhs.mMap); }
  bool getValue(const std::string &key, std::string &outVal) const;
  void addValue(const std::string key, const std::string val);

  // void setExpires();//TODO:
  // void getExpires();//TODO:

  void addAttribute(const std::string &attr);

  bool hasAttribute(const std::string &attr) const;

  std::string outSetCookieNoHeader() const;
  std::string outSetCookie() const;

  bool empty() const { return mMap.empty(); }

private:
  CookiesMap mMap;
};

} // namespace Fei::Http

#endif