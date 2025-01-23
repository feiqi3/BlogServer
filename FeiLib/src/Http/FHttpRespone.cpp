#include "Http/FHttpDef.h"
#include "Http/FHttpResponse.h"
#include <sstream>

#define LINE_BREAKER "\r\n"

namespace Fei::Http{
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

    if (!mBody.empty()) {
        ss << "Content-Length: " << mBody.size();
        ss << LINE_BREAKER;
        ss << mBody;
    }

    return ss.str();
  }
    
}