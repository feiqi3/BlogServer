#include "Http/FHttpRequestParser.h"
#include "FLogger.h"
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
  for (cnt; cnt < view.size(); ++cnt) {
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
FHttpContext FHttpParser::parse() {
  // Readline and parse
  auto lineView = newLine(0);
  ;
  uint32 cursor = 0;
  Method method = parseMethod(lineView, cursor);

  return FHttpContext();
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
}
} // namespace Fei::Http
