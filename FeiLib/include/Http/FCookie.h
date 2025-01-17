#ifndef FCOOKIE_H
#define FCOOKIE_H

#include <string>
#include <map>

namespace Fei::Http {
using CookiesMap = std::map<std::string, std::string>;

class FCookie {
public:
  bool getValue(const std::string &key, std::string &outVal) const;
  void addValue(const std::string key, const std::string val);
  
  //void setExpires();//TODO:
  //void getExpires();//TODO:

  void addAttribute(const std::string& attr);

  bool hasAttribute(const std::string& attr)const;

  std::string outSetCookie()const;

private:
  CookiesMap mMap;
};

} // namespace Fei::Http

#endif