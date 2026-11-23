#include "Http/FHttpDef.h"
#include "Http/FHttpResponse.h"
#include <sstream>

#define LINE_BREAKER "\r\n"

namespace Fei::Http{
  FHttpResponse detail::makeRejectedResponse(StatusCode status, const RequestRejectedCallback& callback) {
    FHttpResponse response;
    response.setStatusCode(status);
    if (!callback) return response;
    try {
      callback(status, response);
      if (!response.isDeferred()) {
        response.setStatusCode(status);
        return response;
      }
    } catch (...) {
      // An application callback cannot abort protocol processing.
    }
    response = FHttpResponse{};
    response.setStatusCode(status);
    return response;
  }

  std::string FHttpResponse::toString() const{
    std::stringstream ss;
    ss<<versionToStr(mVersion)<<" "<<statusCodeToStr(mStatus)<<LINE_BREAKER;

    for(auto&& [header,val] : mHeaders){
        ss<<header<<": "<<val<<LINE_BREAKER;
    }

    for(auto&& cookie :mCookies){
        if(cookie.empty())continue;
        ss << cookie.outSetCookie()<<LINE_BREAKER;
    }

    ss << "Content-Length: " << mBody.size() << LINE_BREAKER;
    ss << LINE_BREAKER;
    ss << mBody;
    auto&& retRes = ss.str();
    return retRes;
  }

  void FHttpResponse::traversalHeader(std::function<bool(const std::pair<std::string, std::string>&)>func)const 
  {
      for (const auto& pair : mHeaders) {
          bool shouldContinue = func(pair);
          if (!shouldContinue)return;
      }
  }
    
}
