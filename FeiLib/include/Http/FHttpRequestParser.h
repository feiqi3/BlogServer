#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "../FBufferReader.h"
#include "FCookie.h"
#include "FHttpDef.h"
#include "Http/FHttpDef.h"

#include <map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Fei::Http {


class F_API FHttpContext {
public:
  friend class FHttpParser;
  friend class FHttpRequest;
public:
  FHttpContext() {}
  Method getMethod() const { return mMethod; }
  Version getHttpVersion() const { return mHttpVersion; }
  bool getHeader(const std::string &key, std::string &outVal) const;
  bool getQuery(const std::string &key, std::string &outVal) const;
  const std::string& getRequestPath()const { return mRequestPath; }
  std::string_view getRequestBody() const;
  
  const auto& getCookies()const{return cookies;}
  const HeaderMap& headers() const { return mHeaders; }
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


  enum class EState {
    RequestLine,
    Headers,
    Body,
    Done,
    Error,
    Rejected
  };

public:
  FHttpParser() {}
  bool parse(FBufferReader &buffer);
  EState getState()const{return state_;}
  StatusCode errorStatus() const { return errorStatus_; }
  FHttpContext& getContext() { return mCtx; }
  void setHeadersCallback(std::function<bool(const FHttpContext&)> callback) { headersCallback_ = std::move(callback); }
  bool hasHeadersCallback() const { return static_cast<bool>(headersCallback_); }
private:
  void parseRequestLine(const std::string &line);
  void parseQuery(const std::string &line);
  void parseCookies(const std::string &line);
  bool parseBodyLength();
private:
  EState state_ = EState::RequestLine;
  std::string lineBuf_;
  size_t contentLength_ = 0;
  size_t headerBytes_ = 0;
  StatusCode errorStatus_ = StatusCode::_400;

  FHttpContext mCtx;
  std::function<bool(const FHttpContext&)> headersCallback_;
};
} // namespace Fei::Http
#endif
