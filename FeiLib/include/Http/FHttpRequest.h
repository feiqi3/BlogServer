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
class F_API FHttpRequest {
public:
  FHttpRequest(FHttp2Parser& parser);
  FHttpRequest( FHttpParser& parser);
  FHttpRequest(const FHttpRequestBuilder& builder);
  bool isValid() const { return mIsValid; }
  Method getMethod() const;
  Version getHttpVersion() const;
  bool getHeader(const std::string &key, std::string &outVal) const;
  inline void eraseHeader(const std::string &key) {
    mHttpCtx.mHeaders.erase(key);
  }
  const std::string& getPath()const;
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
};
}; // namespace Fei::Http

#endif
