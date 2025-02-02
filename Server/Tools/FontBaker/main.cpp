#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <codecvt>
#include <locale>
#include <cstdlib>
#include "nlohmann/json.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct Rect {
    int x, y, w, h;
};

class ImagePacker {
private:
    int canvasWidth, canvasHeight;      
    std::vector<Rect> placedRects;      

public:
    ImagePacker(int width, int height)
        : canvasWidth(width), canvasHeight(height) {}

    bool overlaps(const Rect& a, const Rect& b) {
        return !(a.x + a.w <= b.x ||   
            a.x >= b.x + b.w ||   
            a.y + a.h <= b.y ||   
            a.y >= b.y + b.h);    
    }
    bool findPosition(int w, int h, int& outX, int& outY) {
        for (int y = 0; y <= canvasHeight - h; y++) {
            for (int x = 0; x <= canvasWidth - w; x++) {
                Rect candidate = { x, y, w, h };
                bool conflict = false;
                for (const auto& placed : placedRects) {
                    if (overlaps(candidate, placed)) {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) {
                    outX = x;
                    outY = y;
                    placedRects.push_back(candidate);
                    return true;
                }
            }
        }
        return false;
    }
};


std::vector<unsigned char> loadBinaryFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开文件 " << filename << std::endl;
        return {};
    }
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
}

std::string loadTextFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "无法打开文本文件 " << filename << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 将 UTF-8 转换为 UTF-32 Unicode 码点序列
std::u32string utf8_to_utf32(const std::string& utf8) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.from_bytes(utf8);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cout << "Usage: " << argv[0] << "<font_file> <text_file> <font_size> <out_image_size_x> <out_image_size_x>" << std::endl;
        return -1;
    }

    std::string fontPath = argv[1];
    std::string textFilename = argv[2];
    float fontSize = std::atof(argv[3]);
    if (fontSize <= 0) {
        std::cerr << "无效的字体大小：" << argv[3] << std::endl;
        return -1;
    }

    int imgSizeX = std::atof(argv[4]);
    int imgSizeY = std::atof(argv[5]);
    if (imgSizeX <= 0) {
        std::cerr << "无效的图片大小：" << argv[4] << std::endl;
        return -1;
    }
    if (imgSizeY <= 0) {
        std::cerr << "无效的图片大小：" << argv[5] << std::endl;
        return -1;
    }

    std::string text = loadTextFile(textFilename);
    if (text.empty()) {
        std::cerr << "文本为空或者读取失败" << std::endl;
        return -1;
    }

    std::u32string unicodeText = utf8_to_utf32(text);

    std::vector<unsigned char> fontBuffer = loadBinaryFile(fontPath);
    if (fontBuffer.empty()) {
        std::cerr << "加载字体文件失败: " << fontPath << std::endl;
        return -1;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontBuffer.data(), 0)) {
        std::cerr << "无法初始化字体" << std::endl;
        return -1;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, fontSize);

    nlohmann::json sdf_json;

    // SDF 参数设置
    float onedge_value = 128.0f;
    float pixel_dist_scale = fontSize; // 此处用字体大小作为距离域缩放参数，可根据需求调整

    std::vector<uint8_t> image(imgSizeX * imgSizeY, '\0');

    ImagePacker packer(imgSizeX,imgSizeY);

    for (char32_t ch : unicodeText) {
        if (ch == U' ' || ch == U'\n' || ch == U'\r' || ch == U'\t')
            continue;

        // 获取对应字符的 glyph index
        int glyphIndex = stbtt_FindGlyphIndex(&font, static_cast<int>(ch));

        int width, height, xoff, yoff;
        unsigned char* sdf_bitmap = stbtt_GetGlyphSDF(&font, scale, glyphIndex,5 ,
            onedge_value, pixel_dist_scale,
            &width, &height, &xoff, &yoff);

        int xPos = 0, yPos = 0;
        bool findPos = packer.findPosition(width, height, xPos, yPos);

        if (!findPos) {
            std::cerr << "图片没有足够的空间容纳字符 U+" << std::hex << static_cast<int>(ch) << std::dec
                << " 的 SDF 大小: " << width << "x" << height << std::endl;
            continue;
        }

        if (sdf_bitmap) {
            sdf_json[std::to_string(static_cast<int>(ch))] = { { "w", width }, {"h",height}, {"xoff",xoff}, {"yoff", yoff} };
            std::cout << "字符 U+" << std::hex << static_cast<int>(ch) << std::dec
                << " 的 SDF 大小: " << width << "x" << height
                << ", xoff: " << xoff << ", yoff: " << yoff << std::endl;
            
            int offset = 0;
            for (int y = yPos; y < yPos + height; ++y) {
                int x = xPos;
                memcpy(image.data() + y * imgSizeX + xPos, sdf_bitmap + offset, width);
                offset += width;
            }

            free(sdf_bitmap);
        }
        else {
            std::cerr << "生成字符 U+" << std::hex << static_cast<int>(ch) << std::dec << " 的 SDF 失败" << std::endl;
        }
    }
    stbi_write_jpg("output.jpg", imgSizeX, imgSizeY, 1, image.data(), 100);
    std::ofstream outJson("output.json");
    if (!outJson.is_open()) {
        std::cerr<<"创建文件\"output.json\"失败"<<"\n";
    }
    outJson << sdf_json;
    return 0;
}