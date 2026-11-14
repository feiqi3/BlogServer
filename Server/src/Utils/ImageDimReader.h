#pragma once
#include <string>
namespace Blog::Utils {
// Try to fetch image dimensions from a URL. Returns true on success.
// Uses HTTP/HTTPS client (OpenSSL TLS for https). 3-tier format parser.
// Called from thread pool workers; this is the synchronous work.
bool readImageDims(const std::string& url, int& outWidth, int& outHeight);
} // namespace Blog::Utils
