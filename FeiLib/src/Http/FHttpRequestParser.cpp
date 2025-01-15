#include "Http/FHttpRequestParser.h"
#include "FBufferReader.h"
#include "FLogger.h"
#include "Http/FHttpDef.h"
#include <string>
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
    if (view[cnt] != ' ') {
      break;
    }
  }
  return cnt;
}

uint32 findFirstSpace(FBufferView &view, uint32 begin = 0) {
  uint32 cnt = begin;
  for (cnt; cnt < view.size(); ++cnt) {
    if (view[cnt] == ' ') {
      break;
    }
  }
  return cnt;
}

} // namespace
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
    ;
    uint32 cursor = 0;
    cursor = findFirstNotSpace(lineView);
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
    std::string header = std::string((char *)&lineView[cursor], findColon);
    cursor = findColon + 1;
    std::string content =
        std::string((char *)&lineView[cursor], lineView.size());
    map.insert({header, content});
    oldView = lineView;
  }
  lineView = newLine(&lineView);
  uint32 cursor = findFirstNotSpace(lineView, 0);
  if (lineView.isEOF()) {
    return map;
  }

  if (!(lineView.size() == 0 || lineView.size() - 1 == cursor)) {
    return map;
  }

  lineView = newLine(&lineView);
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

  HttpQueryMap queryMap;
  std::string path;
  if (!parsePath(lineView, path, queryMap, cursor) && path.length() == 0) {
    // Do something?
  }
  Version ver;
  if (!parseVersion(lineView, ver, cursor)) {
    // Do something?
  }

  auto headerMap = parseHeader(lineView);
  return true;
}

bool FHttpParser::parseVersion(FBufferView &view, Http::Version &outVersion,
                               uint32 &cursor) {
  auto end = findFirstNotSpace(view, cursor);
  auto cursorSave = cursor;
  cursor = end + 1;
  outVersion = Version::Unknown;
  if (end - cursorSave < 8) {
    return false;
  }
  if (view[cursor + 5] == 1) {
    if (view[cursorSave + 7] == 1) {
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
  auto end = findFirstNotSpace(view, cursor);
  if (end - cursor > HttpMaxRequestPathLen) {
    Logger::instance()->log("HttpParser", lvl::warn,
                            "Too long http request path {} out of limit {}",
                            end - cursor, HttpMaxRequestPathLen);
    return false;
  }
  int hasQuestion = -1;
  for (auto i = 0u; i < end; ++i) {
    // Find question
    if (view[i] == '?') {
      hasQuestion = i;
      cursor = i + 1;
      break;
    }
  }

  if (hasQuestion > 0) {
    outPath = std::string((char *)&view[0], hasQuestion);
  } else {
    outPath = std::string((char *)&view[0], end);
    cursor = end + 1;
    return true;
  }

  int marker = hasQuestion;
  bool findK = false;
  std::string key, val;
  for (auto i = hasQuestion + 1; (uint32)i < end; ++i) {
    if (view[i] == '=') {
      if (i - marker - 1 < 0) {
        Logger::instance()->log("HttpParser", lvl::warn, "Bad query.");
        return false;
      }
      key = std::string((char *)&view[marker], i - 1 - marker);
      marker = i + 1;
      findK = true;
    } else if (view[i] == '&') {
      if (!findK || i - marker - 1 < 0) {
        Logger::instance()->log("HttpParser", lvl::warn, "Bad query.");
        return false;
      }
      val = std::string((char *)&view[marker], i - 1);
      outmap[val] = key;
      findK = false;
      marker = i + 1;
    }
  }

  cursor = end + 1;
  return true;
}

Http::Method FHttpParser::parseMethod(FBufferView &inView, uint32 &cursor) {
  auto findTokenEnd = findFirstNotSpace(inView, cursor);
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
    return mBuffer.readLineNoPop();
  }
  if (lastView == 0) {
    mBuffer.popLine();
  } else {
    mBuffer.expireView(*lastView);
  }
  auto ret = mBuffer.readLineNoPop();
  _parseContext.line++;
  _parseContext.curLineBeg = 0;
  return ret;
}
} // namespace Fei::Http
