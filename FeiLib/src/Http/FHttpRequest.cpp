#include "Http/FHttpRequest.h"
#include "Http/FHttpRequestParser.h"
#include <memory>
#include <string>
#include <string_view>

namespace Fei::Http {

FHttpRequest::FHttpRequest(FBufferReader &buffer)
    : mHttpCtx(std::make_shared<FHttpContext>()) {
  FHttpParser parser(buffer);
  mIsValid = parser.parse(*mHttpCtx);
}

  Method FHttpRequest::getMethod() const{
    return mHttpCtx->getMethod();
  }
  Version FHttpRequest::getHttpVersion() const{
    return mHttpCtx->getHttpVersion();
  }
  bool FHttpRequest::getHeader(const std::string &key, std::string &outVal) const{
    return mHttpCtx->getHeader(key, outVal);
  }
  bool FHttpRequest::getQuery(const std::string &key, std::string &outVal) const{
    return mHttpCtx->getQuery(key, outVal);
  }

  std::string_view FHttpRequest::getRequestBody() const{
    return mHttpCtx->getRequestBody();
  }

}; // namespace Fei::Http
