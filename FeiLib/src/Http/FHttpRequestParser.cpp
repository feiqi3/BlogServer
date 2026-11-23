#include "Http/FHttpRequestParser.h"
#include "FBufferReader.h"
#include "FLogger.h"
#include "Http/FCookie.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpParserHelper.h"

#include <map>
#include <string>
#include <string_view>

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>
namespace Fei::Http {
namespace {
//%xx and +
bool headerNameIs(std::string_view name, std::string_view lowercase) {
  return std::equal(name.begin(), name.end(), lowercase.begin(), lowercase.end(),
                    [](char a, char b) { return (a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a) == b; });
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

void FHttpParser::parseCookies(const std::string &line) {
  mCtx.cookies.emplace_back(line);
}

void FHttpParser::parseQuery(const std::string &line) {
    ParserUtils::ParsePathLine(line, mCtx.mRequestPath, mCtx.mQueryMap);
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
        auto method = stringToMethod(curBuffer);
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

bool FHttpParser::parseBodyLength() {
  bool hasLength = false;
  for (const auto& [name, value] : mCtx.mHeaders) {
    if (headerNameIs(name, "transfer-encoding")) return false; // HTTP/1 chunked requests are unsupported.
    if (!headerNameIs(name, "content-length")) continue;
    if (hasLength) return false;
    hasLength = true;
    const auto end = value.data() + value.size();
    const auto parsed = std::from_chars(value.data(), end, contentLength_);
    if (parsed.ptr != end || parsed.ec == std::errc::invalid_argument) return false;
    if (parsed.ec == std::errc::result_out_of_range || contentLength_ > HttpMaxRequestBodyBytes) {
      errorStatus_ = StatusCode::_413;
      return false;
    }
  }
  return true;
}

bool FHttpParser::parse(FBufferReader &buffer) {
  while (state_ != EState::Done && state_ != EState::Error && state_ != EState::Rejected &&
         buffer.getBufferReadableSize() > 0) {
    if (state_ == EState::Body) {
      int available = 0;
      const auto* bytes = buffer.peekAll(available);
      const auto length = std::min(static_cast<size_t>(available), contentLength_ - mCtx.mRequestBody.size());
      mCtx.mRequestBody.append(reinterpret_cast<const char*>(bytes), length);
      buffer.expireSize(static_cast<int>(length));
      if (mCtx.mRequestBody.size() == contentLength_) state_ = EState::Done;
      continue;
    }
    if (headerBytes_ == HttpMaxRequestHeaderBytes) {
      errorStatus_ = StatusCode::_431;
      state_ = EState::Error;
      return false;
    }
    ++headerBytes_;
    const auto c = buffer.readNext();
    if (c == '\0') {
      state_ = EState::Error;
      return false;
    }
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
          if (headersCallback_ && !headersCallback_(mCtx)) {
            state_ = EState::Rejected;
            return false;
          }
          if (!parseBodyLength()) {
            state_ = EState::Error;
            return false;
          }
          state_ = contentLength_ > 0 ? EState::Body : EState::Done;

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
    case EState::Body: // Handled in bulk before reading a header byte.
    case EState::Done:
      return true;
    case EState::Error:
    case EState::Rejected:
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
