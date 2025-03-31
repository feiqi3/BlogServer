#include "FileController.h"
#include "Http/FHttpDef.h"
#include "Service/QuickRedirect.h"
#include "Utils/FileReader.h"
#include <algorithm>
#include <sstream>
#include <string>
const std::string ServerWebAssetsPath = SERVER_RESOURCE_DIR "web/assets/";

namespace {

bool parseInt(const char *str, int beg, int end, int &out) {
  if (beg < 0 || end < 0 || end < beg) {
    return false;
  }
  out = 0;
  for (auto i = beg; i < end; ++i) {
    char c = str[i];
    int d = (int)c - (int)'0';
    if (d < 0 || d >= 10) {
      break;
    }
    out = out * 10 + d;
  }
  return true;
}

bool sendByRange(const Fei::Http::FHttpRequest &req,
                 const Blog::MemoryMappedFile &file,
                 Fei::Http::FHttpResponse &response) {
  // has request range?
  std::string receiveRange;
  bool hasRange = req.getHeader("Range", receiveRange);
  if (!hasRange)
    return false;
  // Parse range string.
  int hyphenPos = -1;
  for (int i = 0; i < receiveRange.size(); ++i) {
    char c = receiveRange[i];
    if (c == '-') {
      hyphenPos = i;
      break;
    }
  }

  // pos == 0 is legal?
  if (hyphenPos <= 0) {
    return false;
  }

  int begPos = 0;
  int endPos = 0;

  // Want it all !
  if (hyphenPos + 1 == receiveRange.size()) {

    parseInt(receiveRange.c_str(), 0, hyphenPos, begPos);
    std::clamp(begPos, 0, (int)file.size());
    endPos = file.size();

    // Want it small =v=
  } else {

    if (!parseInt(receiveRange.c_str(), 0, hyphenPos, begPos) ||
        !parseInt(receiveRange.c_str(), hyphenPos + 1, receiveRange.size(),
                  endPos)) {
      return false;
    }
    endPos = std::clamp(endPos, 0, (int)file.size());
    begPos = std::clamp(begPos, 0, endPos);
  }

  std::stringstream contentRange;
  contentRange << "bytes " << begPos << "-" << endPos << "/" << file.size();
  response.addHeader("Content-Range", contentRange.str());
  int toSendSize = endPos - begPos;
  response.setBody(std::string((char *)file.data() + begPos, toSendSize));
  return true;
};

} // namespace

Fei::Http::FHttpResponse
Blog::FileController::getFile(const Fei::Http::FHttpRequest &req,
                              const Fei::Http::FPathVar &var) {
  auto str = var.get("name");
  if (str.empty()) {
    auto ret = Redirector::RedirectTo("/404");
    ret.setStatusCode(Fei::Http::StatusCode::_404);
    return ret;
  }
  MemoryMappedFile file(ServerWebAssetsPath + str, Mode::ReadOnly, 0);
  if (!file.data()) {
    auto ret = Redirector::RedirectTo("/404");
    ret.setStatusCode(Fei::Http::StatusCode::_404);
    return ret;
  }

  Fei::Http::FHttpResponse res;

  // Send By range
  if (sendByRange(req, file, res)) {
    return res;
  }

  std::string filedata((char *)file.data(), file.size());
  res.setBody(filedata);
  return res;
}
