#ifndef FHTTPRESPONSE_H
#define FHTTPRESPONSE_H
#include "FCookie.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequestParser.h"
#include <string>

namespace Fei::Http {
using HeaderMap = std::map<std::string, std::string>;

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
    mHeaders[key] = val;
    return *this;
  }

  FHttpResponse &setContentType(const std::string &type) {
    mHeaders["Content-Type"] = type;
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

  FHttpResponse &setCookie(const FCookie &inCookie) {
    mCookie = inCookie;
    return *this;
  }

  FHttpResponse &setCookie(FCookie &&inCookie) {
    mCookie = (std::move(inCookie));
    return *this;
  }

  std::string toString() const;

private:
  Version mVersion;
  StatusCode mStatus;
  HeaderMap mHeaders;
  std::string mBody;
  FCookie mCookie;
};

} // namespace Fei::Http

#endif