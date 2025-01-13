#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "FHttpDef.h"
#include "../FBufferReader.h"

#include <string>
#include <map>

namespace Fei {


class F_API FHttpContext {
    public:
    private:
    std::map<std::string,std::string> mHeaders;
};

class FHttpParser {
public:
  static FHttpContext parse(FBufferReader &inReader);

private:
  static FBufferView parseHost();
  static FBufferView parseUserAgent();

  //Method and method data.
  static std::pair<Http::Method,FBufferView> parseMethod();
  static Http::Version parseVersion();

  static FBufferView parseBody();
};
} // namespace Fei
#endif