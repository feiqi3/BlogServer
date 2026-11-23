#pragma once
#include <cstdint>
#include <string_view>

namespace Blog::Utils {
enum class ImageFormat { Unknown, Jpeg, Png, Gif, WebP, Bmp };
struct ImageInfo {
    int width = 0;
    int height = 0;
    ImageFormat format = ImageFormat::Unknown;
};
// Metadata only: never downloads, expands pixels, logs URLs or changes a database.
// Recognizing metadata does not assert that every pixel of a file decodes.
bool readImageInfo(std::string_view bytes, ImageInfo& out,
                   std::uint64_t maxPixels = 200'000'000);
const char* imageFormatName(ImageFormat format) noexcept;
} // namespace Blog::Utils
