#ifndef FHTTPREQUEST_H
#define FHTTPREQUEST_H
#include "Http/FHttpRequestParser.h"
#pragma once
#include <memory>
namespace Fei {
class FBufferReader;
} // namespace Fei

namespace Fei::Http {
class FHttpContext;
class FHttpRequest {
public:
bool isValid()const{return mIsValid;}
private:
  FHttpRequest(FBufferReader &buffer);
  std::shared_ptr<FHttpContext> mHttpCtx;
  bool mIsValid = true;
};
}; // namespace Fei::Http

#endif
