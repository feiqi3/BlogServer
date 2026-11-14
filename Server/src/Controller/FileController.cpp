#include "FileController.h"
#include "Http/FHttpDef.h"
#include "Service/QuickRedirect.h"
#include "Utils/FileReader.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
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

// ---- Precompressed sibling delivery -------------------------------------
// The blogweb build emits <file>.gz / <file>.br siblings for text assets in
// dist/assets (see vite.config.js precompressStatic). When the client's
// Accept-Encoding matches and there is no Range request, the precompressed
// body is sent directly (no runtime compression CPU cost).
// Content-Type is inferred by FeiLib from the URL path, so the sibling file
// does not affect type detection.

bool isCompressibleType(const std::string &name) {
  static const char *sCompressible[] = {".js",   ".css", ".json", ".svg",
                                        ".webmanifest", ".txt", ".xml"};
  auto dot = name.rfind('.');
  if (dot == std::string::npos || dot + 1 == name.size()) {
    return false;
  }
  std::string ext = name.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  for (auto *e : sCompressible) {
    if (ext == e) {
      return true;
    }
  }
  return false;
}

// Word-boundary matching for Accept-Encoding values: avoids substring false
// positives such as "brag" matching "br". acceptLower must be pre-lowercased;
// a token only counts when flanked by string start/end, comma or whitespace.
bool acceptsToken(const std::string &acceptLower, const char *token) {
  const auto tokLen = std::strlen(token);
  auto pos = acceptLower.find(token);
  while (pos != std::string::npos) {
    bool leftOk = pos == 0 || acceptLower[pos - 1] == ',' ||
                  acceptLower[pos - 1] == ' ' || acceptLower[pos - 1] == '\t';
    auto end = pos + tokLen;
    bool rightOk = end == acceptLower.size() ||
                   acceptLower[end] == ',' || acceptLower[end] == ' ' ||
                   acceptLower[end] == '\t' || acceptLower[end] == ';';
    if (leftOk && rightOk) {
      return true;
    }
    pos = acceptLower.find(token, end);
  }
  return false;
}

// Encoding choice only; callers must filter by isCompressibleType first.
const char *encodingVariantFor(const Fei::Http::FHttpRequest &req) {
  std::string accept;
  if (!req.getHeader("Accept-Encoding", accept)) {
    req.getHeader("accept-encoding", accept);
  }
  if (accept.empty()) {
    return nullptr;
  }
  std::transform(accept.begin(), accept.end(), accept.begin(), ::tolower);
  if (acceptsToken(accept, "br")) {
    return ".br";
  }
  if (acceptsToken(accept, "gzip") || acceptsToken(accept, "x-gzip")) {
    return ".gz";
  }
  return nullptr;
}

const char *contentEncodingFor(const char *variant) {
  return std::string_view(variant) == ".br" ? "br" : "gzip";
}

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

  // Emit Vary for every variant (br/gz/identity) of compressible types so a
  // shared cache cannot serve one variant to a client that cannot decode it.
  const bool compressible = isCompressibleType(str);
  if (compressible) {
    res.addHeader("Vary", "Accept-Encoding");
  }

  // Precompressed variant first (skipped for Range requests: 206 semantics
  // operate on the identity body).
  std::string rangeHeader;
  bool hasRange = req.getHeader("Range", rangeHeader) ||
                  req.getHeader("range", rangeHeader);
  if (compressible && !hasRange) {
    const char *variant = encodingVariantFor(req);
    if (variant) {
      // MemoryMappedFile failure semantics: unopenable => data() == nullptr
      // => fall back to the identity body.
      MemoryMappedFile comp(ServerWebAssetsPath + str + variant,
                            Mode::ReadOnly, 0);
      if (comp.data()) {
        res.addHeader("Content-Encoding", contentEncodingFor(variant));
        res.setBody(std::string((char *)comp.data(), comp.size()));
        return res;
      }
    }
  }

  // Send By range
  if (sendByRange(req, file, res)) {
    return res;
  }

  std::string filedata((char *)file.data(), file.size());
  res.setBody(filedata);
  return res;
}
