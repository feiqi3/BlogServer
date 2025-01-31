#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "iconv.h"
#include "uchardet.h"

#include <iostream>
#include <memory>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#define FONT_SIZE  64   // 字体大小
#define SDF_WIDTH  64   // 生成 SDF 纹理的尺寸
#define SDF_PADDING 5   // 额外的填充以保证边界不丢失

// 读取字体文件
unsigned char* ReadFile(const char* filename, int* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open font file!\n");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char* data = (unsigned char*)malloc(*size);
    fread(data, 1, *size, file);
    fclose(file);
    return data;
}

int main(int argc, char* argv[]) {
    assert(argc == 5);
    if (argc != 6)return -1;
    std::string fontPath = argv[1];
    int fontsize = std::stoi(argv[2]);
    if (fontsize <= 0)return -1;
    int fontPicSize = std::stoi(argv[3]);
    if (fontPicSize <= 0)return -1;
    int fontPadding = std::stoi(argv[4]);
    if (fontPadding <= 0)return -1;
    int wordsSize = 0;
    unsigned char* words = ReadFile("words.txt", &wordsSize);
    if (!words) return -1;
    int ttfSize = 0;
    unsigned char* fontBuffer = ReadFile("font.ttf", &ttfSize);
    if (!fontBuffer) { return -1; }

    auto detector = uchardet_new();
    uchardet_handle_data(detector,(char*)words, wordsSize);
    auto charSetName = uchardet_get_charset(detector);
    uchardet_delete(detector);
    if (charSetName == 0) {
        printf("Gving words in unknown charset.");
        return -1;
    }
    auto ic = iconv_open("UTF−8", charSetName);

    size_t wordsize = wordsSize;
    size_t newBufferSize = wordsSize * 3;
    std::u8string  wordsUTF8;
    {
        auto newBuffer = std::unique_ptr<char[]>(new char[newBufferSize]);
        auto bufferNew = newBuffer.get();
        size_t newCvtSIze =
        iconv(ic, (char**)&words, &wordsize, &bufferNew, &newBufferSize);
        wordsUTF8 = std::u8string((char8_t*)bufferNew, newCvtSIze);
    }
    iconv_close(ic);
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontBuffer, stbtt_GetFontOffsetForIndex(fontBuffer, 0))) {
        printf("Failed to initialize font!\n");
        free(fontBuffer);
        return -1;
    }

    // 生成 SDF 字体纹理
    float pixelDistScale = 1.0f / FONT_SIZE; // 控制距离场缩放
    unsigned char* sdfBitmap = stbtt_GetCodepointSDF(&font, pixelDistScale, 'A', SDF_PADDING, 128, 64, NULL);

    if (sdfBitmap) {
        // 这里可以将 SDF 数据存入纹理或保存为图像
        FILE* file = fopen("sdf_A.raw", "wb");
        fwrite(sdfBitmap, 1, SDF_WIDTH * SDF_WIDTH, file);
        fclose(file);

        stbtt_FreeSDF(sdfBitmap, NULL);
    }
    else {
        printf("Failed to generate SDF bitmap!\n");
    }

    free(fontBuffer);
    return 0;
}