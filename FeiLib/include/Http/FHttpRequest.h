#pragma once
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
  void setAddr(const FSocketAddr& addr) {
	  mAddr = addr;
  }

  const FSocketAddr& getAddr()const { return mAddr; }
private:
  std::shared_ptr<FHttpContext> mHttpCtx;
  FSocketAddr mAddr;
  bool mIsValid = true;
};
}; // namespace Fei::Http

#endif
