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
  bool getQuery(const std::string &key, std::string &outVal) const;
  std::string_view getRequestBody() const;

private:
  std::shared_ptr<FHttpContext> mHttpCtx;
  bool mIsValid = true;
};
}; // namespace Fei::Http

#endif
