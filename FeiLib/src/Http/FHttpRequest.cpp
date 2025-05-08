#include "Http/FHttpRequest.h"
#include "Http/FHttpRequestParser.h"
#include <memory>
#include <string>
#include <string_view>

namespace Fei::Http {

FHttpRequest::FHttpRequest( FHttpParser& parser)
{
  if(parser.getState() != FHttpParser::EState::Done){
    mIsValid = false;
  }
  mHttpCtx = std::move(parser.getContext());
}

int FHttpRequest::getCookieSize() const {
  return mHttpCtx.getCookies().size();
}

const FCookie &FHttpRequest::getCookie(int i) const {
  return mHttpCtx.getCookies().at(i);
}

Method FHttpRequest::getMethod() const { return mHttpCtx.getMethod(); }
Version FHttpRequest::getHttpVersion() const {
  return mHttpCtx.getHttpVersion();
}
bool FHttpRequest::getHeader(const std::string &key,
                             std::string &outVal) const {
  return mHttpCtx.getHeader(key, outVal);
}
const std::string &FHttpRequest::getPath() const {
  return mHttpCtx.getRequestPath();
}
bool FHttpRequest::getQuery(const std::string &key, std::string &outVal) const {
  return mHttpCtx.getQuery(key, outVal);
}

std::string_view FHttpRequest::getRequestBody() const {
  return mHttpCtx.getRequestBody();
}

}; // namespace Fei::Http
