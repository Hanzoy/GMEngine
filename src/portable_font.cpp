#include "portable_font.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include "roboto_regular.ttf.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace gm {
namespace {
struct Font {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info{};
    Font() {
        if (!stbtt_InitFont(&info, s_robotoRegularTtf, 0))
            throw std::runtime_error("Cannot initialize bundled font");
    }
};
Font &font() {
    static Font value;
    return value;
}
} // namespace
void setPortableFont(std::vector<uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 32 * 1024 * 1024)
        throw std::runtime_error("Invalid font size");
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, bytes.data(), 0))
        throw std::runtime_error("Invalid font");
    font().bytes = std::move(bytes);
    font().info = info;
}
FontBitmap rasterText(const std::wstring &text, int size) {
    auto &info = font().info;
    float scale = stbtt_ScaleForPixelHeight(&info, float(size));
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    float advance = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        int a, b;
        stbtt_GetCodepointHMetrics(&info, int(text[i]), &a, &b);
        advance += a * scale;
        if (i + 1 < text.size())
            advance += stbtt_GetCodepointKernAdvance(&info, int(text[i]), int(text[i + 1])) * scale;
    }
    FontBitmap out;
    out.width = std::clamp(int(std::ceil(advance)) + 4, 1, 4096);
    out.height = std::max(1, int(std::ceil((ascent - descent) * scale)) + 4);
    out.pixels.resize(size_t(out.width) * out.height, 0);
    float x = 0;
    int baseline = int(std::ceil(ascent * scale));
    for (size_t i = 0; i < text.size() && x < out.width; ++i) {
        int x0, y0, x1, y1, a, b;
        int cp = int(text[i]);
        stbtt_GetCodepointBitmapBox(&info, cp, scale, scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            std::vector<uint8_t> alpha(size_t(w) * h);
            stbtt_MakeCodepointBitmap(&info, alpha.data(), w, h, w, scale, scale, cp);
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < w; ++xx) {
                    int dx = int(std::round(x)) + x0 + xx, dy = baseline + y0 + yy;
                    if (dx >= 0 && dx < out.width && dy >= 0 && dy < out.height) {
                        auto &p = out.pixels[size_t(dy) * out.width + dx];
                        p = 0xffffffu | (std::max(p >> 24, uint32_t(alpha[size_t(yy) * w + xx])) << 24);
                    }
                }
        }
        stbtt_GetCodepointHMetrics(&info, cp, &a, &b);
        x += a * scale;
        if (i + 1 < text.size())
            x += stbtt_GetCodepointKernAdvance(&info, cp, int(text[i + 1])) * scale;
    }
    return out;
}
} // namespace gm
