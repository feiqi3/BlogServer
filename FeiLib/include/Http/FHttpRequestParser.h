#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "../FBufferReader.h"
#include "FHttpDef.h"
#include "Http/FHttpDef.h"


#include <map>
#include <string>


namespace Fei::Http {

using HttpQueryMap = std::map<std::string, std::string>;
using HeaderMap = std::map<std::string, std::string>;
using CookieMap = std::map<std::string, std::string>;
class F_API FHttpContext {
public:
  friend class FHttpParser;

public:
  Method getMethod() const { return mMethod; }
  Version getHttpVersion() const { return mHttpVersion; }
  bool getHeader(const std::string &key, std::string &outVal) const;
  bool getQuery(const std::string &key, std::string &outVal) const;
  bool getRequestBody(std::string &outBody) const;

private:
  FHttpContext() {}
  Version mHttpVersion;
  std::string mRequestPath;
  Method mMethod;
  HeaderMap mHeaders;
  HttpQueryMap mQueryMap;
  CookieMap mCookieMap;
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
  CookieMap parseCookie()const;
  FBufferView newLine(FBufferView *lastView);

private:
  struct {
    uint32 line = 0;
  } _parseContext;
  FBufferReader &mBuffer;
};
} // namespace Fei::Http
#endif