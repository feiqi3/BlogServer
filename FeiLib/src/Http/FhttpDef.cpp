#include "Http/FHttpDef.h"

namespace Fei::Http {
const char *statusCodeToStr(StatusCode code) {
  switch (code) {
  case StatusCode::_100:
    return "100 Continue";
  // 200 case
  case StatusCode::_200:
    return "200 OK";
  case StatusCode::_201:
    return "201 Create";
  case StatusCode::_202:
    return "202 Accepted";
  case StatusCode::_204:
    return "204 No Content";
  case StatusCode::_301:
    return "301 Moved Permanently";
  case StatusCode::_302:
    return "302 Found";
  case StatusCode::_303:
    return "303 See Other";
  case StatusCode::_304:
    return "304 Not Modified";
  case StatusCode::_307:
    return "307 Temporary Redirect";
  case StatusCode::_308:
    return "308 Permanent Redirect";
  case StatusCode::_400:
    return "400 Bad Request";
  case StatusCode::_401:
    return "401 Unauthorized";
  case StatusCode::_403:
    return "403 Forbidden";
  case StatusCode::_404:
    return "404 Not Found";
  case StatusCode::_405:
    return "405 Method Not Allowed";
  case StatusCode::_408:
    return "408 Request Timeout";
  case StatusCode::_418:
    return "418 I'm a teapot";
  case StatusCode::_429:
    return "429 Too Many Requests";
  case StatusCode::_500:
    return "500 Internal Server Error";
  case StatusCode::_501:
    return "501 Not Implemented";
  case StatusCode::_502:
    return "502 Bad Gateway";
  case StatusCode::_505:
    return "505 HTTP Version Not Supported";
    break;
  }
  // default
  return statusCodeToStr(StatusCode::_200);
}

const char* methodToStr(Method method)
{
    switch (method)
    {
    case Fei::Http::Method::GET:return "GET";
        break;
    case Fei::Http::Method::POST:return "POST";
        break;
    case Fei::Http::Method::HEAD:return "HEAD";
        break;
    case Fei::Http::Method::PUT:return "PUT";
        break;
    case Fei::Http::Method::DELETE:return "DELETE";
        break;
    case Fei::Http::Method::PATCH:return "PATCH";
        break;
    case Fei::Http::Method::CONNECT:return "CONNECT";
        break;
    case Fei::Http::Method::OPTIONS:return "OPTIONS";
        break;
    case Fei::Http::Method::TRACE:return "TRACE";
        break;
    case Fei::Http::Method::Invalid:return "Invalid";
        break;
    default:
        return nullptr;
        break;
    }
}

const char *versionToStr(Version version) {
  switch (version) {

  case Version::Http10:
    return "HTTP/1.0";
  case Version::Unknown:
  case Version::Http11:
  default:
    return "HTTP/1.1";
    break;
  }
}

} // namespace Fei::Http