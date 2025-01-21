#include "Http/FHttpRequestParser.h"
#include "FBufferReader.h"
#include "FLogger.h"
#include "Http/FCookie.h"
#include "Http/FHttpDef.h"
#include <string>
#include <string_view>

namespace Fei::Http {
namespace {
const std::string MethodsName[] = {
    "GET",   "POST",    "HEAD",    "PUT",   "DELETE",
    "PATCH", "CONNECT", "OPTIONS", "TRACE", "Invalid",
};

const std::map<std::string, Method> MethodsMap = {
    {"GET", Method::GET},         {"POST", Method::POST},
    {"HEAD", Method::HEAD},       {"PUT", Method::PUT},
    {"DELETE", Method::DELETE},   {"PATCH", Method::PATCH},
    {"CONNECT", Method::CONNECT}, {"OPTIONS", Method::OPTIONS},
    {"TRACE", Method::TRACE},
};
uint32 findFirstNotSpace(FBufferView &view, uint32 begin = 0) {
  uint32 cnt = begin;
  for (; cnt < view.size(); ++cnt) {
    if (view[cnt] != ' ' && view[cnt] != '\r' && view[cnt] != '\n') {
      break;
    }
  }
  return cnt;
}

uint32 findFirstSpace(FBufferView &view, uint32 begin = 0) {
  uint32 cnt = begin;
  for (; cnt < view.size(); ++cnt) {
    if (view[cnt] == ' ' || view[cnt] == '\r' || view[cnt] == '\n') {
      break;
    }
  }
  return cnt;
}

} // namespace

bool FHttpContext::getHeader(const std::string &key,
                             std::string &outVal) const {
  auto itor = mHeaders.find(key);
  if (itor == mHeaders.end()) {
    return false;
  }

  outVal = itor->second;
  return true;
}

std::string_view FHttpContext::getRequestBody() const {
  auto itor = mHeaders.find("__body");
  if (itor == mHeaders.end()) {
    return {};
  }

  return itor->second;
}

bool FHttpContext::getQuery(const std::string &key, std::string &outVal) const {
  auto itor = mQueryMap.find(key);
  if (itor == mQueryMap.end()) {
    return false;
  }

  outVal = itor->second;
  return true;
}

const std::string &FHttpParser::MethodToString(Method method) {
  switch (method) {
  case Fei::Http::Method::GET:
    return MethodsName[0];
    break;
  case Fei::Http::Method::POST:
    return MethodsName[1];
    break;
  case Fei::Http::Method::HEAD:
    return MethodsName[2];
    break;
  case Fei::Http::Method::PUT:
    return MethodsName[3];
    break;
  case Fei::Http::Method::DELETE:
    return MethodsName[4];
    break;
  case Fei::Http::Method::PATCH:
    return MethodsName[5];
    break;
  case Fei::Http::Method::CONNECT:
    return MethodsName[6];
    break;
  case Fei::Http::Method::OPTIONS:
    return MethodsName[7];
    break;
  case Fei::Http::Method::TRACE:
    return MethodsName[8];
    break;
  case Fei::Http::Method::Invalid:
  default:
    return MethodsName[9];
    break;
  }
}
Method FHttpParser::StringToMethod(const std::string &in) {
  auto itor = MethodsMap.find(in);
  if (itor != MethodsMap.end()) {
    return itor->second;
  }
  return Method::Invalid;
}

HeaderMap FHttpParser::parseHeader(FBufferView &oldView) {
  HeaderMap map;
  FBufferView lineView(oldView);
  while (1) {
    lineView = newLine(&oldView);

    uint32 cursor = 0;
    cursor = findFirstNotSpace(lineView);
    // Blank line
    if (lineView.size() == 0 || lineView.size() - 1 == cursor)
      break;
    // To long?
    if (lineView.size() > HttpMaxRequestPathLen) {
      Logger::instance()->log("HttpParser", lvl::warn,
                              "Too long http header {} out of limit {}",
                              lineView.size(), HttpMaxRequestPathLen);
      continue;
    }

    // Find colon
    int findColon = -1;
    for (int i = 0; i < (int)lineView.size(); ++i) {
      if (lineView[i] == ':') {
        findColon = i;
        break;
      }
    }
    if (findColon == -1) {
      Logger::instance()->log(
          "HttpParser", lvl::warn, "Wired header: {}",
          std::string((char *)&lineView[0], lineView.size()));
      continue;
    }
    std::string header =
        std::string((char *)&lineView[cursor], findColon - cursor);
    cursor = findColon + 1;
    uint32 _crlfOffset = 0;
    if (lineView.size() - cursor >= 2) {
      if (lineView[lineView.size() - 2] == '\r' &&
          lineView[lineView.size() - 1] == '\n') {
        _crlfOffset = 2;
      }
    }

    std::string content = std::string((char *)&lineView[cursor],
                                      lineView.size() - cursor - _crlfOffset);
    map.insert({header, content});
    oldView = lineView;
  }

  lineView = newLine(&lineView);

  if (lineView.isEOF()) {
    return map;
  }

  uint32 bufferSize = mBuffer.readTo(0, 0);
  std::string body;
  body.reserve(bufferSize + 1);
  mBuffer.readTo(body.data(), bufferSize + 1);
  map.insert({"__body", std::move(body)});

  return map;
}

bool FHttpParser::parse(FHttpContext &ctx) {
  // Readline and parse
  auto lineView = newLine(0);

  uint32 cursor = 0;

  Method method = parseMethod(lineView, cursor);

  ctx.mMethod = method;

  HttpQueryMap queryMap;
  std::string path;
  if (!parsePath(lineView, path, queryMap, cursor) && path.length() == 0) {
    return false;
    // Do something?
  }

  ctx.mRequestPath = std::move(path);
  ctx.mQueryMap = std::move(queryMap);

  Version ver;
  if (!parseVersion(lineView, ver, cursor)) {
    return false;
    // Do something?
  }

  ctx.mHttpVersion = ver;

  auto headerMap = parseHeader(lineView);

  auto cookieEqualRange = headerMap.equal_range("Cookie");
  for(auto itor = cookieEqualRange.first;itor != cookieEqualRange.second;itor++){
    FCookie cookie;
    parseCookie(cookie, itor->second);
    ctx.cookies.emplace_back(cookie);
  }
  headerMap.erase(cookieEqualRange.first,cookieEqualRange.second);
  ctx.mHeaders = std::move(headerMap);

  return true;
}

bool FHttpParser::parseVersion(FBufferView &view, Http::Version &outVersion,
                               uint32 &cursor) {
  auto beg = findFirstNotSpace(view, cursor);
  auto end = findFirstSpace(view, beg);
  if (end == beg)return false;
  cursor = end + 1;
  outVersion = Version::Unknown;
  if (end - beg < 8) {
    return false;
  }
  if (view[beg + 5] == '1') {
    if (view[beg + 7] == '1') {
      outVersion = Version::Http11;
    } else {
      outVersion = Version::Http10;
    }
    return true;
  }
  return false;
}

bool FHttpParser::parsePath(FBufferView &view, std::string &outPath,
                            HttpQueryMap &outmap, uint32 &cursor) {
  auto beg = findFirstNotSpace(view, cursor);
  auto end = findFirstSpace(view, cursor);
  if (end - beg == 0)return false;

  if (end - beg > HttpMaxRequestPathLen) {
    Logger::instance()->log("HttpParser", lvl::warn,
                            "Too long http request path {} out of limit {}",
                            end - beg, HttpMaxRequestPathLen);
    return false;
  }
  int hasQuestion = -1;
  for (auto i = beg; i < end; ++i) {
    // Find question
    if (view[i] == '?') {
      hasQuestion = i;
      cursor = i + 1;
      break;
    }
  }

  if (hasQuestion > 0) {
    outPath = std::string((char *)&view[beg], hasQuestion - beg);
  } else {
    outPath = std::string((char *)&view[beg], end - beg);
    cursor = end + 1;
    return true;
  }

  int marker = hasQuestion + 1;
  bool findK = false;
  std::string key, val;
  for (auto i = hasQuestion + 1; (uint32)i < end; ++i) {
    if (view[i] == '=') {
      if (i - marker - 1 < 0) {
        Logger::instance()->log("HttpParser", lvl::warn, "Bad query.");
        return false;
      }
      key = std::string((char *)&view[marker], i - marker);
      marker = i + 1;
      findK = true;
    } else if (view[i] == '&') {
      if (!findK) {
        continue;
      }
      val = std::string((char *)&view[marker], i - marker);
      outmap[key] = val;
      findK = false;
      marker = i + 1;
    }
  }

  if (findK) {
    val = std::string((char *)&view[marker], end - marker);
    outmap[key] = val;
  }

  cursor = end + 1;
  return true;
}

Http::Method FHttpParser::parseMethod(FBufferView &inView, uint32 &cursor) {
  auto findTokenBeg = findFirstNotSpace(inView, cursor);
  cursor = findTokenBeg;
  auto findTokenEnd = findFirstSpace(inView, findTokenBeg);
  std::string method((char *)&inView[cursor], findTokenEnd - cursor);
  // next
  cursor = findTokenEnd + 1;
  auto itor = MethodsMap.find(method);
  Method ret = Method::Invalid;
  if (itor != MethodsMap.end()) {
    ret = itor->second;
  }

  if (ret == Method::Invalid) {
    Logger::instance()->log("HttpParser", lvl::err,
                            "Unknown request method {}.", method);
  }

  return ret;
}
FBufferView FHttpParser::newLine(FBufferView *lastView) {
  if (_parseContext.line == 0) {
    _parseContext.line++;
    return mBuffer.readLineNoPop();
  }
  if (lastView == 0) {
    mBuffer.popLine();
  } else {
    mBuffer.expireView(*lastView);
  }
  auto ret = mBuffer.readLineNoPop();
  _parseContext.line++;
  return ret;
}

void FHttpParser::parseCookie(FCookie &inCookie,
                              const std::string &cookieData) const {
  auto i = 0u;
  for (i = 0u; i < cookieData.size(); ++i) {
    if (cookieData[i] != ' ') {
      break;
    }
  }
  bool findKey = false;
  std::string key, val;
  uint32 saveMarker = i;
  for (; i < cookieData.size(); ++i) {
    if (cookieData[i] == ';') {
      if (findKey) {
        findKey = false;
        val = std::string(&cookieData[saveMarker], i - saveMarker);
        inCookie.addValue(key, val);
        val = "";
      } else {
        inCookie.addAttribute(key);
      }
      key = "";
      i++;
      saveMarker = i;
    } else if (cookieData[i] == '=') {
      findKey = true;
      key = std::string(&cookieData[saveMarker], i - saveMarker);
      i++;
      saveMarker = i;
    }
  }
}

} // namespace Fei::Http
