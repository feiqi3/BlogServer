#include "Http/FHttpRequestParser.h"
#include "FBufferReader.h"
#include "FLogger.h"
#include "Http/FCookie.h"
#include "Http/FHttpDef.h"
#include <map>
#include <string>
#include <string_view>

#include <algorithm>
#include <sstream>
#include <string_view>
namespace Fei::Http {
namespace {
//%xx and +
static std::string urlDecode(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '%') {
      if (i + 2 < s.size()) {
        int hi = std::isdigit(s[i + 1]) ? s[i + 1] - '0'
                                        : std::toupper(s[i + 1]) - 'A' + 10;
        int lo = std::isdigit(s[i + 2]) ? s[i + 2] - '0'
                                        : std::toupper(s[i + 2]) - 'A' + 10;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      }
    } else if (c == '+') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool isAllDigits(const std::string &s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isdigit(c);
  });
}

inline void trimSpaces(std::string &s) {
  // 找到首个非空格字符的位置
  size_t first = s.find_first_not_of(' ');
  if (first == std::string::npos) {
    // 整串都是空格，直接清空
    s.clear();
    return;
  }
  // 找到末尾第一个非空格字符的位置
  size_t last = s.find_last_not_of(' ');
  // 删除末尾多余空格
  s.erase(last + 1);
  // 删除开头多余空格
  s.erase(0, first);
}

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

std::string_view FHttpContext::getRequestBody() const { return mRequestBody; }

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

void FHttpParser::parseCookies(const std::string &line) {
  mCtx.cookies.emplace_back(line);
}

void FHttpParser::parseQuery(const std::string &line) {
  auto qm = line.find('?');
  if (qm == std::string::npos) {
    return; // No query
  }

  std::string query = line.substr(qm + 1);
  mCtx.mRequestPath = line.substr(0, qm);
  // Remove fragment identifier
  auto hash = query.find('#');
  if (hash != std::string::npos) {
    query.resize(hash);
  }

  std::stringstream ss(query);
  std::string pair;
  while (std::getline(ss, pair, '&')) {
    if (pair.empty())
      continue;

    auto eq = pair.find('=');
    std::string key, value;
    if (eq != std::string::npos) {
      key = urlDecode(pair.substr(0, eq));
      value = urlDecode(pair.substr(eq + 1));
    } else {
      key = urlDecode(pair);
      value = "";
    }
    this->mCtx.mQueryMap.insert({std::move(key), std::move(value)});
  }
}

void FHttpParser::parseRequestLine(const std::string &line) {
  assert(state_ == EState::RequestLine);
  std::string curBuffer;
  int step = 0;
  // 1. method, 2. path, 3. version
  for (int i = 0; i <= (int)line.size(); i++) {
    char c = line[i];
    if (c == ' ' || i == (int)line.size()) {
      switch (step) {
      case 0: {
        auto method = StringToMethod(curBuffer);
        if (method == Method::Invalid) {
          state_ = EState::Error;
          return;
        } else {
          mCtx.mMethod = method;
        }
      } break;

      case 1: {
        mCtx.mRequestPath = curBuffer;
        parseQuery(mCtx.mRequestPath);
      } break;
      case 2: {
        if (curBuffer == "HTTP/1.1") {
          mCtx.mHttpVersion = Version::Http11;
        } else if (curBuffer == "HTTP/1.0") {
          mCtx.mHttpVersion = Version::Http10;
        } else {
          state_ = EState::Error;
          return;
        }
      } break;
      }
      step++;
      curBuffer.clear();
      continue;
    } else {
      curBuffer.push_back(c);
    }
  }

  if (step != 3) {
    state_ = EState::Error;
    return;
  }
}

bool FHttpParser::parse(FBufferReader &buffer) {
  char c;
  while ((c = buffer.readNext()) != '\0' &&
         (state_ != EState::Done && state_ != EState::Error)) {
    switch (state_) {

      // parse request line
    case EState::RequestLine: {
      lineBuf_.push_back(c);
      if (lineBuf_.size() >= 2 && lineBuf_.end()[-2] == '\r' &&
          lineBuf_.end()[-1] == '\n') {
        lineBuf_.resize(lineBuf_.size() - 2);
        parseRequestLine(lineBuf_);
        if (state_ == EState::Error) {
          lineBuf_.clear();
          return false;
        }
        lineBuf_.clear();
        state_ = EState::Headers;
      }
    } break;

    // parse headers
    case EState::Headers: {
      lineBuf_.push_back(c);
      if (lineBuf_.size() >= 2 && lineBuf_.end()[-2] == '\r' &&
          lineBuf_.end()[-1] == '\n') {
        // End of headers
        if (lineBuf_ == "\r\n") {
          auto it = mCtx.mHeaders.find("Content-Length");
          if (it != mCtx.mHeaders.end()) {
            if (!isAllDigits(it->second)) {
              state_ = EState::Error;
              return false;
            }

            contentLength_ = std::stoul(it->second);
            mCtx.mRequestBody.reserve(contentLength_);

            state_ = (contentLength_ > 0 ? EState::Body : EState::Done);
          } else {
            // 没声明长度，按 RFC 7230 对请求消息可视为 0-length
            state_ = EState::Done;
          }

          // parse Cookies
          auto range = mCtx.mHeaders.equal_range("Cookie");
          for (auto it = range.first; it != range.second; ++it) {
            parseCookies(it->second);
          }
          mCtx.mHeaders.erase(range.first, range.second);

        } else {
          // 解析单条 header： "Name: value"
          auto pos = lineBuf_.find(':');
          // 去除 "\r\n"
          if (lineBuf_.size() >= 2 && lineBuf_.end()[-2] == '\r' &&
              lineBuf_.end()[-1] == '\n') {
            lineBuf_.resize(lineBuf_.size() - 2);
          }
          if (pos != std::string::npos) {
            std::string name = lineBuf_.substr(0, pos);
            std::string value = lineBuf_.substr(pos + 1);
            trimSpaces(value);

            mCtx.mHeaders.insert({std::move(name), std::move(value)});
          } else {
            // Not valid header
            mCtx.mHeaders.insert({std::move(lineBuf_), ""});
          }
        }
        lineBuf_.clear();
      }
    } break;
    case EState::Body:
      mCtx.mRequestBody.push_back(c);
      ++bodyBytesRead_;
      if (bodyBytesRead_ >= contentLength_) {
        state_ = EState::Done;
      }
      break;
    case EState::Done:
      return true;
    case EState::Error:
      return false;
      break;
    }
  }

  if (state_ == EState::Done) {
    return true;
  } else {
    return false;
  }
}

} // namespace Fei::Http
