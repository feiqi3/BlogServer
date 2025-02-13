#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "../FBufferReader.h"
#include "FCookie.h"
#include "FHttpDef.h"
#include "Http/FHttpDef.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fei::Http {

using HttpQueryMap = std::map<std::string, std::string>;
using HeaderMap = std::multimap<std::string, std::string>;
class F_API FHttpContext {
public:
  friend class FHttpParser;

public:
  FHttpContext() {}
  FHttpContext(FHttpContext &&rhs) {
    mHttpVersion = rhs.mHttpVersion;
    mMethod = rhs.mMethod;
    mRequestPath = std::move(rhs.mRequestPath);
    cookies = std::move(rhs.cookies);
    mHeaders = std::move(rhs.mHeaders);
    mQueryMap = std::move(rhs.mQueryMap);
  }
  Method getMethod() const { return mMethod; }
  Version getHttpVersion() const { return mHttpVersion; }
  bool getHeader(const std::string &key, std::string &outVal) const;
  bool getQuery(const std::string &key, std::string &outVal) const;
  const std::string& getRequestPath()const { return mRequestPath; }
  std::string_view getRequestBody() const;
  

private:
  Method mMethod;
  std::string mRequestPath;
  Version mHttpVersion;

  HttpQueryMap mQueryMap;
  HeaderMap mHeaders;
  std::vector<FCookie> cookies;
};

// Fix me: need a more easy and faster parser.
// Current: stupid stupid code.
class F_API FHttpParser {
public:
  static const std::string &MethodToString(Method method);
  static Method StringToMethod(const std::string &);

public:
  FHttpParser(FBufferReader &buffer) : mBuffer(buffer) {}
  bool parse(FHttpContext &ctx);

private:
  // Method and method data.
  Http::Method parseMethod(FBufferView &inView, uint32 &cursor);
  bool parseVersion(FBufferView &view, Http::Version &outVersion,
                    uint32 &cursor);
  bool parsePath(FBufferView &inView, std::string &outPath,
                 HttpQueryMap &outmap, uint32 &cursor);
  HeaderMap parseHeader(FBufferView &oldView);
  FBufferView newLine(FBufferView *lastView);
  void parseCookie(FCookie &inCookie, const std::string &cookieData) const;
private:
  struct {
    uint32 line = 0;
  } _parseContext;
  FBufferReader &mBuffer;
};
} // namespace Fei::Http
#endif