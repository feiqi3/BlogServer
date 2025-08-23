#include "Http/FHttpRequest.h"
#include "Http/FHttpRequestParser.h"
#include "Http/FHttp2Helper.h"
#include <memory>
#include <string>
#include <string_view>

namespace Fei::Http {
FHttpRequest::FHttpRequest(FHttp2Parser& parser)
{
    //This will only happen when A STREAM IS FINISH   
    auto itPair = parser.mHeaders.equal_range("cookie");
    for (auto it = itPair.first; it != itPair.second; ++it) {
        FCookie cookie(it->second);
        mHttpCtx.cookies.push_back(cookie);
    }
    parser.mHeaders.erase("cookie");
    mHttpCtx.mHeaders = std::move(parser.mHeaders);
    mHttpCtx.mMethod = parser.mMethod;
    mHttpCtx.mRequestPath = parser.mPath;
    mHttpCtx.mHttpVersion = Version::Http2;
    mHttpCtx.mRequestBody = std::move(parser.mData);
    mHttpCtx.mQueryMap = std::move(parser.mQuery);

}
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
