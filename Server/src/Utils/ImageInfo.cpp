#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
#include "Utils/ImageInfo.h"
#include <climits>
#include <cstring>

namespace Blog::Utils {
namespace {
std::uint32_t little32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
bool webp(std::string_view bytes, int& width, int& height) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WEBP", 4)) return false;
    const std::uint64_t declared = std::uint64_t(little32(p + 4)) + 8;
    if (declared < 12) return false;
    for (std::size_t pos = 12; pos <= bytes.size() && bytes.size() - pos >= 8;) {
        if (pos > declared || declared - pos < 8) return false;
        const auto count = little32(p + pos + 4);
        const auto* d = p + pos + 8;
        const auto available = bytes.size() - pos - 8;
        if (count > declared - pos - 8) return false;
        if (!std::memcmp(p + pos, "VP8X", 4)) {
            if (count != 10 || available < 10) return false;
            width = 1 + int(std::uint32_t(d[4]) | (std::uint32_t(d[5]) << 8) | (std::uint32_t(d[6]) << 16));
            height = 1 + int(std::uint32_t(d[7]) | (std::uint32_t(d[8]) << 8) | (std::uint32_t(d[9]) << 16));
            return true;
        }
        if (!std::memcmp(p + pos, "VP8L", 4)) {
            if (count < 5 || available < 5 || d[0] != 0x2f || (d[4] & 0xe0)) return false;
            auto bits = little32(d + 1);
            width = int((bits & 0x3fff) + 1);
            height = int(((bits >> 14) & 0x3fff) + 1);
            return true;
        }
        if (!std::memcmp(p + pos, "VP8 ", 4)) {
            if (count < 10 || available < 10 || (d[0] & 1) || d[3] != 0x9d || d[4] != 1 || d[5] != 0x2a) return false;
            width = d[6] | ((d[7] & 0x3f) << 8);
            height = d[8] | ((d[9] & 0x3f) << 8);
            return true;
        }
        const std::uint64_t advance = 8ULL + count + (count & 1);
        if (advance > bytes.size() - pos) return false;
        pos += static_cast<std::size_t>(advance);
        if (pos > declared) return false;
    }
    return false;
}
}

bool readImageInfo(std::string_view bytes, ImageInfo& out, std::uint64_t maxPixels) {
    out = {};
    if (bytes.empty() || bytes.size() > INT_MAX || maxPixels == 0) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    ImageInfo info;
    if (bytes.size() >= 12 && !std::memcmp(p, "RIFF", 4) && !std::memcmp(p + 8, "WEBP", 4)) {
        info.format = ImageFormat::WebP;
        if (!webp(bytes, info.width, info.height)) return false;
    } else {
        if (bytes.size() >= 8 && !std::memcmp(p, "\x89PNG\r\n\x1a\n", 8)) info.format = ImageFormat::Png;
        else if (bytes.size() >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff) info.format = ImageFormat::Jpeg;
        else if (bytes.size() >= 6 && (!std::memcmp(p, "GIF87a", 6) || !std::memcmp(p, "GIF89a", 6))) info.format = ImageFormat::Gif;
        else if (bytes.size() >= 2 && p[0] == 'B' && p[1] == 'M') info.format = ImageFormat::Bmp;
        else return false;
        int channels = 0;
        if (!stbi_info_from_memory(p, static_cast<int>(bytes.size()), &info.width, &info.height, &channels)) return false;
    }
    if (info.width <= 0 || info.height <= 0 ||
        std::uint64_t(info.width) * std::uint64_t(info.height) > maxPixels) return false;
    out = info;
    return true;
}

const char* imageFormatName(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Jpeg: return "jpeg";
    case ImageFormat::Png: return "png";
    case ImageFormat::Gif: return "gif";
    case ImageFormat::WebP: return "webp";
    case ImageFormat::Bmp: return "bmp";
    default: return "unknown";
    }
}
} // namespace Blog::Utils
