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
  Method getMethod() const { return mMethod; }
  Version getHttpVersion() const { return mHttpVersion; }
  bool getHeader(const std::string &key, std::string &outVal) const;
  bool getQuery(const std::string &key, std::string &outVal) const;
  const std::string& getRequestPath()const { return mRequestPath; }
  std::string_view getRequestBody() const;
  
  const auto& getCookies()const{return cookies;}
private:
  Method mMethod;
  std::string mRequestPath;
  Version mHttpVersion;
  std::string mRequestBody;
  HttpQueryMap mQueryMap;
  HeaderMap mHeaders;
  std::vector<FCookie> cookies;
};

// Fix me:faster parser,less copy
class F_API FHttpParser {
public:
  static const std::string &MethodToString(Method method);
  static Method StringToMethod(const std::string &);

  enum class EState {
    RequestLine,
    Headers,
    Body,
    Done,
    Error
  };

public:
  FHttpParser() {}
  bool parse(FBufferReader &buffer);
  EState getState()const{return state_;}
  FHttpContext& getContext() { return mCtx; }
private:
  void parseRequestLine(const std::string &line);
  void parseQuery(const std::string &line);
  void parseCookies(const std::string &line);
private:
  EState state_ = EState::RequestLine;
  std::string lineBuf_;
  size_t contentLength_ = 0;
  size_t bodyBytesRead_ = 0;

  bool chunked_ = false;

  FHttpContext mCtx;
};
} // namespace Fei::Http
#endif