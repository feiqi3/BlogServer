#pragma once
#include "FDef.h"
#include "Http/FCookie.h"
#include "Http/FHttpRequestBuilder.h"
#include "Http/FHttpRequestParser.h"
#ifndef FHTTPREQUEST_H
#define FHTTPREQUEST_H
#include <memory>
#include "Http/FHttpDef.h"

namespace Fei {
class FBufferReader;
} // namespace Fei

namespace Fei::Http {
	class FHttp2Parser;
class FHttpContext;

// Borrows parser storage for the duration of the request-header callback.
struct FHttpRequestHeaders {
  Method method;
  const std::string& path;
  const HeaderMap& fields;
  const FSocketAddr& peer;

  Method getMethod() const { return method; }
  const std::string& getPath() const { return path; }
  const HeaderMap& headers() const { return fields; }
  const FSocketAddr& getAddrIn() const { return peer; }
};

class F_API FHttpRequest {
public:
  FHttpRequest(FHttp2Parser& parser);
  FHttpRequest( FHttpParser& parser);
  FHttpRequest(const FHttpRequestBuilder& builder);
  explicit FHttpRequest(const FHttpContext& headers) : mHttpCtx(headers) {}
  bool isValid() const { return mIsValid; }
  void setApplicationContext(std::shared_ptr<void> context) const { mApplicationContext = std::move(context); }
  const std::shared_ptr<void>& applicationContext() const { return mApplicationContext; }
  Method getMethod() const;
  Version getHttpVersion() const;
  bool getHeader(const std::string &key, std::string &outVal) const;
  inline void eraseHeader(const std::string &key) {
    mHttpCtx.mHeaders.erase(key);
  }
  const std::string& getPath()const;
  const HeaderMap& headers() const { return mHttpCtx.headers(); }
  bool getQuery(const std::string &key, std::string &outVal) const;
  std::string_view getRequestBody() const;
  void setAddrIn(const FSocketAddr& addr) {
	  mAddrIn = addr;
  }
  void setAddrHost(const FSocketAddr& addr) {
	  mAddrHost = addr;
  }

  inline void traverseHeaders(const std::function<bool(const std::pair<std::string, std::string>&)>& func) const {
    for (const auto& header : mHttpCtx.mHeaders) {
      if (!func(header)) {
        break;
      }
    }
  }

  int getCookieSize()const;
  const FCookie& getCookie(int i)const;
  const FSocketAddr& getAddrIn()const { return mAddrIn; }
  const FSocketAddr& getAddrHost()const { return mAddrHost; }
private:
  FHttpContext mHttpCtx;
  FSocketAddr mAddrIn;
  FSocketAddr mAddrHost;
  bool mIsValid = true;
  mutable std::shared_ptr<void> mApplicationContext;
};
}; // namespace Fei::Http

#endif
