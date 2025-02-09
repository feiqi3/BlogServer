#pragma once
#include "FDef.h"
#ifndef FHTTPREQUEST_H
#define FHTTPREQUEST_H
#include <memory>
#include "Http/FHttpDef.h"

namespace Fei {
class FBufferReader;
} // namespace Fei

namespace Fei::Http {

class FHttpContext;
class F_API FHttpRequest {
public:
  FHttpRequest(FBufferReader &buffer);
  bool isValid() const { return mIsValid; }
  Method getMethod() const;
  Version getHttpVersion() const;
  bool getHeader(const std::string &key, std::string &outVal) const;
  const std::string& getPath()const;
  bool getQuery(const std::string &key, std::string &outVal) const;
  std::string_view getRequestBody() const;
  void setAddrIn(const FSocketAddr& addr) {
	  mAddrIn = addr;
  }
  void setAddrHost(const FSocketAddr& addr) {
	  mAddrHost = addr;
  }

  const FSocketAddr& getAddrIn()const { return mAddrIn; }
  const FSocketAddr& getAddrHost()const { return mAddrHost; }
private:
  std::shared_ptr<FHttpContext> mHttpCtx;
  FSocketAddr mAddrIn;
  FSocketAddr mAddrHost;
  bool mIsValid = true;
};
}; // namespace Fei::Http

#endif
