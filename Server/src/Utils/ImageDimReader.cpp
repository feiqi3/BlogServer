#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ImageDimReader.h"
#include "FLogger.h"
#include "FSocket.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequestBuilder.h"
#include "miniz.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
// winsock2.h defines POLL* as macros that clash with the Fei::POLL* constants
// from FSocket.h; we only use WSAGetLastError from winsock here.
#undef POLLIN
#undef POLLOUT
#undef POLLPRI
#undef POLLERR
#undef POLLHUP
#undef POLLNVAL
#undef POLLRDNORM
#undef POLLRDBAND
#undef POLLWRNORM
#undef POLLWRBAND
#endif

namespace Blog::Utils {
namespace {

const size_t kMaxBodyBytes = 64 * 1024; // enough for any image header
const size_t kMaxRawBytes = 128 * 1024; // safety cap on total download

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

bool parseUrl(const std::string& url, std::string& scheme, std::string& host,
              int& port, std::string& path) {
  auto pos = url.find("://");
  if (pos == std::string::npos)
    return false;
  scheme = toLower(url.substr(0, pos));
  if (scheme != "http" && scheme != "https")
    return false;
  port = (scheme == "https") ? 443 : 80;
  size_t restStart = pos + 3;
  size_t pathStart = url.find('/', restStart);
  std::string authority = (pathStart == std::string::npos)
                              ? url.substr(restStart)
                              : url.substr(restStart, pathStart - restStart);
  path = (pathStart == std::string::npos) ? "/" : url.substr(pathStart);
  auto colon = authority.find(':');
  if (colon != std::string::npos) {
    host = authority.substr(0, colon);
    std::string portStr = authority.substr(colon + 1);
    if (portStr.empty() ||
        !std::all_of(portStr.begin(), portStr.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; }))
      return false;
    port = std::atoi(portStr.c_str());
  } else {
    host = authority;
  }
  return !host.empty();
}

// Inflate gzip (or zlib) payload via miniz. Gzip header is stripped first.
bool inflatePayload(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
  if (len < 2)
    return false;
  bool isGzip = (data[0] == 0x1f && data[1] == 0x8b);
  const uint8_t* inPtr = data;
  size_t inLen = len;
  if (isGzip) {
    if (inLen < 10)
      return false;
    size_t off = 10;
    uint8_t flags = data[3];
    if (flags & 0x04) { // FEXTRA
      if (inLen < off + 2)
        return false;
      off += 2 + (data[off] | (data[off + 1] << 8));
    }
    if (flags & 0x08) { // FNAME
      while (off < inLen && data[off] != 0)
        ++off;
      if (off >= inLen)
        return false;
      ++off;
    }
    if (flags & 0x10) { // FCOMMENT
      while (off < inLen && data[off] != 0)
        ++off;
      if (off >= inLen)
        return false;
      ++off;
    }
    if (flags & 0x02) { // FHCRC
      off += 2;
    }
    if (off > inLen)
      return false;
    inPtr = data + off;
    inLen = len - off;
  }
  mz_stream stream;
  std::memset(&stream, 0, sizeof(stream));
  int windowBits = isGzip ? -MZ_DEFAULT_WINDOW_BITS : MZ_DEFAULT_WINDOW_BITS;
  if (mz_inflateInit2(&stream, windowBits) != MZ_OK)
    return false;
  stream.next_in = inPtr;
  stream.avail_in = (unsigned int)inLen;
  std::vector<uint8_t> buf(kMaxBodyBytes);
  stream.next_out = buf.data();
  stream.avail_out = (unsigned int)buf.size();
  int status = mz_inflate(&stream, MZ_FINISH);
  size_t produced = buf.size() - stream.avail_out;
  out.assign(buf.data(), buf.data() + produced);
  mz_inflateEnd(&stream);
  // Partial output is acceptable (we only need header bytes).
  return (status == MZ_STREAM_END) || (status == MZ_BUF_ERROR && produced > 0);
}

// Decode a chunked body. Stops early on incomplete data; needs only the
// first chunk bytes to read image headers.
bool decodeChunked(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
  size_t pos = 0;
  while (pos < len) {
    size_t lineEnd = pos;
    while (lineEnd < len && data[lineEnd] != '\n')
      ++lineEnd;
    if (lineEnd >= len)
      break; // incomplete chunk size line
    std::string sizeStr;
    size_t i = pos;
    while (i < lineEnd && data[i] != ';' && data[i] != '\r')
      sizeStr.push_back((char)data[i++]);
    long chunkSize = std::strtol(sizeStr.c_str(), nullptr, 16);
    if (chunkSize <= 0)
      break; // last chunk (0)
    size_t dataStart = lineEnd + 1;
    if (dataStart + (size_t)chunkSize + 2 > len)
      break; // incomplete chunk data
    out.insert(out.end(), data + dataStart, data + dataStart + chunkSize);
    pos = dataStart + (size_t)chunkSize + 2;
  }
  return !out.empty();
}

// ---------------------------------------------------------------------------
// Bounded socket helpers.
//
// On this host, setting SO_RCVTIMEO on a proxied connection black-holes all
// incoming data (recv times out even though the peer responded). Instead of
// relying on SO timeouts we keep the socket non-blocking and drive every
// operation through Fei::FPoll, which also lets OpenSSL's WANT_READ/WANT_WRITE
// progress the TLS handshake.
// ---------------------------------------------------------------------------

const int64_t kIoDeadlineMs = 12000; // total budget for the whole exchange

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Wait until the socket is readable/writable or the timeout expires.
bool waitForSocket(Fei::Socket s, bool wantRead, int timeoutMs) {
  Fei::FPollfd pfd;
  pfd.fd = s;
  pfd.events = wantRead ? Fei::POLLIN : Fei::POLLOUT;
  pfd.revents = 0;
  int r = Fei::FPoll(&pfd, 1, timeoutMs);
  if (r <= 0)
    return false;
  return (pfd.revents & pfd.events) != 0;
}

// Remaining time in ms, never negative.
int remainingMs(int64_t deadline) {
  return (int)std::max<int64_t>(0, deadline - nowMs());
}

// Full SSL failure detail: SSL_get_error + OpenSSL error queue + OS error.
std::string sslErrorDetail(SSL* ssl, int ret) {
  std::ostringstream oss;
  oss << "ssl_error=" << SSL_get_error(ssl, ret);
  unsigned long e;
  while ((e = ERR_get_error()) != 0) {
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    oss << " | " << buf;
  }
#ifdef _WIN32
  int wsaErr = WSAGetLastError();
  if (wsaErr != 0)
    oss << " | WSA=" << wsaErr;
#else
  if (errno != 0)
    oss << " | errno=" << errno;
#endif
  return oss.str();
}

// Returns 1 on success, 0 on timeout, -1 on hard error (lastRet receives the
// failing SSL_connect return value so the caller can build error detail).
int sslConnectBounded(SSL* ssl, Fei::Socket s, int64_t deadline, int& lastRet) {
  for (;;) {
    int ret = SSL_connect(ssl);
    if (ret == 1)
      return 1;
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      int remain = remainingMs(deadline);
      if (remain <= 0 || !waitForSocket(s, err == SSL_ERROR_WANT_READ, remain))
        return 0;
      continue;
    }
    lastRet = ret;
    return -1;
  }
}

bool sslWriteAllBounded(SSL* ssl, Fei::Socket s, const char* data, int len,
                        int64_t deadline) {
  int off = 0;
  while (off < len) {
    int n = SSL_write(ssl, data + off, len - off);
    if (n > 0) {
      off += n;
      continue;
    }
    int err = SSL_get_error(ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      int remain = remainingMs(deadline);
      if (remain <= 0 || !waitForSocket(s, err == SSL_ERROR_WANT_READ, remain))
        return false;
      continue;
    }
    return false;
  }
  return true;
}

// Returns >0 bytes read, 0 on clean close, -1 on error/timeout.
int sslReadBounded(SSL* ssl, Fei::Socket s, char* buf, int len,
                   int64_t deadline) {
  for (;;) {
    int n = SSL_read(ssl, buf, len);
    if (n > 0)
      return n;
    int err = SSL_get_error(ssl, n);
    if (err == SSL_ERROR_ZERO_RETURN)
      return 0; // clean close
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      int remain = remainingMs(deadline);
      if (remain <= 0 || !waitForSocket(s, err == SSL_ERROR_WANT_READ, remain))
        return -1;
      continue;
    }
    return -1;
  }
}

bool sockSendAllBounded(Fei::Socket s, const char* data, int len,
                        int64_t deadline) {
  int off = 0;
  while (off < len) {
    int sent = 0;
    Fei::SocketStatus st = Fei::Send(s, data + off, len - off, sent);
    if (st == Fei::SocketStatus::Success && sent > 0) {
      off += sent;
      continue;
    }
    int remain = remainingMs(deadline);
    if (remain <= 0 || !waitForSocket(s, false, remain))
      return false;
  }
  return true;
}

// Returns >=0 bytes read, -1 on error/timeout.
int sockRecvBounded(Fei::Socket s, char* buf, int len, int64_t deadline) {
  for (;;) {
    int recvLen = 0;
    Fei::SocketStatus st = Fei::Recv(s, buf, len, Fei::RecvFlag::None, recvLen);
    if (st == Fei::SocketStatus::Success)
      return recvLen;
    int remain = remainingMs(deadline);
    if (remain <= 0 || !waitForSocket(s, true, remain))
      return -1;
  }
}

// Read full HTTP response, returns body bytes (decompressed if gzip).
// Also reports the status code and Location header (for redirects).
bool doHttpRequest(const std::string& host, int port, bool isHttps,
                   const std::string& path, std::vector<uint8_t>& outBody,
                   int& outStatus, std::string& outLocation) {
  outStatus = 0;
  outLocation.clear();
  std::vector<Fei::FSocketAddr> addrs;
  if (!Fei::ResolveHost(host, port, addrs) || addrs.empty())
    return false;
  Fei::Socket s;
  if (Fei::Create(s) != Fei::SocketStatus::Success)
    return false;
  if (Fei::Connect(s, addrs[0]) != Fei::SocketStatus::Success) {
    Fei::Close(s);
    return false;
  }
  // Non-blocking socket: timeouts are enforced via Fei::FPoll below. Setting
  // SO_RCVTIMEO here black-holes incoming data on proxied connections.
  Fei::SetSockOpt(s, Fei::SockOpt::NoneBlock, true);
  const int64_t deadline = nowMs() + kIoDeadlineMs;

  Fei::Http::FHttpRequestBuilder builder;
  builder.setMethod(Fei::Http::Method::GET)
      .setUrl(path)
      .addHeader("Host", host)
      .addHeader("Connection", "close")
      .addHeader("User-Agent", "BlogServer-ImageDimReader/1.0");
  std::string req = builder.build();

  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  if (isHttps) {
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
      Fei::Close(s);
      return false;
    }
    // Internal header-only fetcher: skip cert verification.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_default_verify_paths(ctx);
    ssl = SSL_new(ctx);
    if (!ssl) {
      SSL_CTX_free(ctx);
      Fei::Close(s);
      return false;
    }
    SSL_set_fd(ssl, (int)s);
    SSL_set_tlsext_host_name(ssl, host.c_str()); // SNI for CDN vhosts
    int lastRet = -1;
    int hc = sslConnectBounded(ssl, s, deadline, lastRet);
    if (hc == 0) {
      Fei::Logger::instance()->log(
          Fei::lvl::err, "[ImageDimReader] SSL_connect timed out for {}", host);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      Fei::Close(s);
      return false;
    }
    if (hc < 0) {
      Fei::Logger::instance()->log(
          Fei::lvl::err, "[ImageDimReader] SSL_connect failed for {}: {}", host,
          sslErrorDetail(ssl, lastRet));
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      Fei::Close(s);
      return false;
    }
  }

  bool requestSent =
      isHttps ? sslWriteAllBounded(ssl, s, req.c_str(), (int)req.size(), deadline)
              : sockSendAllBounded(s, req.c_str(), (int)req.size(), deadline);
  if (!requestSent) {
    if (ssl)
      SSL_free(ssl);
    if (ctx)
      SSL_CTX_free(ctx);
    Fei::Close(s);
    return false;
  }

  static const char kHeaderEnd[] = "\r\n\r\n";
  std::vector<uint8_t> raw;
  raw.reserve(16 * 1024);
  char buf[8192];
  size_t headerEndPos = std::string::npos;
  long contentLength = -1;
  bool chunked = false;
  size_t target = kMaxRawBytes;
  while (raw.size() < target) {
    int n = 0;
    if (isHttps) {
      n = sslReadBounded(ssl, s, buf, sizeof(buf), deadline);
      if (n <= 0)
        break;
    } else {
      n = sockRecvBounded(s, buf, sizeof(buf), deadline);
      if (n <= 0)
        break;
    }
    raw.insert(raw.end(), buf, buf + n);
    if (headerEndPos == std::string::npos) {
      auto it = std::search(raw.begin(), raw.end(), kHeaderEnd, kHeaderEnd + 4);
      if (it == raw.end())
        continue;
      headerEndPos = (size_t)(it - raw.begin()) + 4;
      // Parse status line + headers.
      std::string headerBlock((const char*)raw.data(), headerEndPos);
      std::istringstream iss(headerBlock);
      std::string statusLine;
      std::getline(iss, statusLine);
      size_t p1 = statusLine.find(' ');
      if (p1 != std::string::npos) {
        std::string code = statusLine.substr(p1 + 1);
        size_t p2 = code.find(' ');
        if (p2 != std::string::npos)
          code = code.substr(0, p2);
        outStatus = std::atoi(code.c_str());
      }
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos)
          continue;
        std::string name = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        value.erase(value.begin(),
                    std::find_if(value.begin(), value.end(), [](unsigned char c) {
                      return !std::isspace(c);
                    }));
        if (name == "content-length")
          contentLength = std::atol(value.c_str());
        else if (name == "transfer-encoding" &&
                 value.find("chunked") != std::string::npos)
          chunked = true;
        else if (name == "location") {
          while (!value.empty() && std::isspace((unsigned char)value.back()))
            value.pop_back();
          outLocation = value;
        }
      }
      if (outStatus != 200)
        break; // redirects/errors -> caller decides how to proceed
      if (!chunked && contentLength >= 0)
        target =
            headerEndPos + (size_t)std::min<long>(contentLength,
                                                  (long)kMaxBodyBytes);
      else if (chunked)
        target = headerEndPos + kMaxBodyBytes;
      else
        target = kMaxRawBytes;
    }
  }
  if (ssl)
    SSL_free(ssl);
  if (ctx)
    SSL_CTX_free(ctx);
  Fei::Close(s);

  if (headerEndPos == std::string::npos)
    return false;
  // Redirects: no body needed, report status + Location to the caller.
  if (outStatus >= 300 && outStatus < 400)
    return true;
  if (outStatus != 200)
    return false;
  if (raw.size() <= headerEndPos)
    return false;

  const uint8_t* bodyPtr = raw.data() + headerEndPos;
  size_t bodyLen = raw.size() - headerEndPos;
  std::vector<uint8_t> body;
  if (chunked) {
    if (!decodeChunked(bodyPtr, bodyLen, body))
      return false;
  } else {
    body.assign(bodyPtr, bodyPtr + bodyLen);
    if (contentLength >= 0 && (size_t)contentLength < body.size())
      body.resize((size_t)contentLength);
  }
  // Decompress gzip/zlib encoded bodies.
  std::vector<uint8_t> inflated;
  if (inflatePayload(body.data(), body.size(), inflated))
    body = std::move(inflated);
  outBody = std::move(body);
  return true;
}

// WebP RIFF header parser: VP8 (lossy) / VP8L (lossless) / VP8X (extended).
bool parseWebPHeader(const uint8_t* b, size_t len, int& w, int& h) {
  if (len < 30)
    return false;
  if (std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WEBP", 4) != 0)
    return false;
  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t* d = b + pos + 8;
    if (std::memcmp(b + pos, "VP8 ", 4) == 0) {
      if (pos + 8 + 10 > len)
        return false;
      // Frame tag(3) + start code 0x9d 0x01 0x2a, then 14-bit w/h.
      if (d[3] == 0x9d && d[4] == 0x01 && d[5] == 0x2a) {
        w = d[6] | ((d[7] & 0x3f) << 8);
        h = d[8] | ((d[9] & 0x3f) << 8);
        return w > 0 && h > 0;
      }
      return false;
    }
    if (std::memcmp(b + pos, "VP8L", 4) == 0) {
      if (pos + 8 + 5 > len)
        return false;
      if (d[0] == 0x2f) { // lossless signature byte
        uint32_t bits = d[1] | (d[2] << 8) | (d[3] << 16) | (d[4] << 24);
        w = (int)((bits & 0x3fff) + 1);
        h = (int)(((bits >> 14) & 0x3fff) + 1);
        return w > 0 && h > 0;
      }
      return false;
    }
    if (std::memcmp(b + pos, "VP8X", 4) == 0) {
      if (pos + 8 + 10 > len)
        return false;
      // Flags(4) + 24-bit canvas width/height, each +1.
      w = (int)((d[4] | (d[5] << 8) | (d[6] << 16)) + 1);
      h = (int)((d[7] | (d[8] << 8) | (d[9] << 16)) + 1);
      return w > 0 && h > 0;
    }
    uint32_t chunkSize =
        b[pos + 4] | (b[pos + 5] << 8) | (b[pos + 6] << 16) | (b[pos + 7] << 24);
    pos += 8 + chunkSize + (chunkSize & 1); // chunks are 2-byte aligned
  }
  return false;
}

// Resolve a redirect Location against the URL the redirect came from.
// Handles absolute URLs, protocol-relative (//host), absolute-path (/x) and
// relative path (x) forms.
std::string resolveRedirectUrl(const std::string& currentUrl,
                               const std::string& location) {
  if (location.find("://") != std::string::npos)
    return location;
  std::string scheme, host, path;
  int port = 0;
  if (!parseUrl(currentUrl, scheme, host, port, path))
    return location;
  if (location.size() >= 2 && location[0] == '/' && location[1] == '/')
    return scheme + ":" + location;
  std::string base = scheme + "://" + host;
  bool defaultPort = (scheme == "http" && port == 80) ||
                     (scheme == "https" && port == 443);
  if (!defaultPort)
    base += ":" + std::to_string(port);
  if (!location.empty() && location[0] == '/')
    return base + location;
  // Relative path reference: resolve against the current directory.
  size_t slash = path.find_last_of('/');
  std::string dir =
      (slash == std::string::npos) ? "/" : path.substr(0, slash + 1);
  return base + dir + location;
}

} // namespace

bool readImageDims(const std::string& url, int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;
  std::string currentUrl = url;
  for (int redirectCount = 0;; ++redirectCount) {
    std::string scheme, host, path;
    int port = 0;
    if (!parseUrl(currentUrl, scheme, host, port, path))
      return false;
    std::vector<uint8_t> body;
    int status = 0;
    std::string location;
    if (!doHttpRequest(host, port, scheme == "https", path, body, status,
                       location)) {
      Fei::Logger::instance()->log(Fei::lvl::err,
                                   "[ImageDimReader] HTTP fetch failed for {}",
                                   currentUrl);
      return false;
    }
    if (status >= 300 && status < 400) {
      if (redirectCount >= 3 || location.empty())
        return false; // too many redirects or no target
      currentUrl = resolveRedirectUrl(currentUrl, location);
      continue;
    }
    if (status != 200)
      return false;
    if (body.empty())
      return false;
    if (body.size() > kMaxBodyBytes)
      body.resize(kMaxBodyBytes);
    // Tier 1: stbi header info (JPEG/PNG/BMP/PSD/TGA/GIF/HDR/PIC/PNM).
    int w = 0, h = 0, n = 0;
    if (stbi_info_from_memory(body.data(), (int)body.size(), &w, &h, &n) != 0) {
      outWidth = w;
      outHeight = h;
      return true;
    }
    // Tier 2: WebP RIFF chunk parser.
    if (parseWebPHeader(body.data(), body.size(), w, h)) {
      outWidth = w;
      outHeight = h;
      return true;
    }
    // Tier 3: unknown format, caller stores 0/0.
    Fei::Logger::instance()->log(
        Fei::lvl::warn,
        "[ImageDimReader] No parser matched for {} (body {} bytes)",
        currentUrl, body.size());
    return false;
  }
}

} // namespace Blog::Utils
