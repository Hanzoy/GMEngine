#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace gm {
struct FontBitmap {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
};
FontBitmap rasterText(const std::wstring &, int pixels);
// Called before creating the Renderer. TrueType/OTF bytes are copied and retained.
void setPortableFont(std::vector<uint8_t> bytes);
} // namespace gm
