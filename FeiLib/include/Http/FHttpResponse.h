#ifndef FHTTPRESPONSE_H
#define FHTTPRESPONSE_H
#include "FCookie.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequestParser.h"
#include <functional>
#include <string>
#include <vector>

namespace Fei::Http {
using HeaderMap = std::multimap<std::string, std::string>;

class FHttpResponse {
public:
  FHttpResponse() {}

  FHttpResponse &setBody(std::string &&body) {
    mBody = std::move(body);
    return *this;
  }

  FHttpResponse &setBody(const std::string &body) {
    mBody = body;
    return *this;
  }

  FHttpResponse &addHeader(const std::string &key, const std::string &val) {
    mHeaders.insert(key, val);
    return *this;
  }

  FHttpResponse &setContentType(const std::string &type) {
    auto itor = mHeaders.find("Content-Type");
    if (itor != mHeaders.end()) {
      itor->second = type;
    } else {
      mHeaders.insert({"Content-Type", type});
    }
    return *this;
  }

  FHttpResponse &setStatusCode(StatusCode code) {
    mStatus = code;
    return *this;
  }

  FHttpResponse &setHttpVersion(Version version) {
    mVersion = version;
    return *this;
  }

  FHttpResponse &pushCookie(const FCookie &iCookie) {
    mCookies.push_back(iCookie);
    return *this;
  }

  FHttpResponse &emplaceCookie(FCookie &&inCookie) {
    mCookies.emplace_back(std::move(inCookie));
    return *this;
  }

  FHttpResponse &addCookie(const FCookie &inCookie) {
    mCookies.push_back(inCookie);
    return *this;
  }

  FHttpResponse &addCookie(FCookie &&inCookie) {
    mCookies.push_back(std::move(inCookie));
    return *this;
  }

  std::string toString() const;

private:
  Version mVersion;
  StatusCode mStatus;
  HeaderMap mHeaders;
  std::string mBody;
  std::vector<FCookie> mCookies;
};

} // namespace Fei::Http

#endif