#include "image_import.hpp"
#include <fstream>
#include <memory>
#include <stdexcept>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace gm {
HdrImage importHdr(const std::filesystem::path &path) {
    auto size = std::filesystem::file_size(path);
    if (!size || size > MaxImageBytes)
        throw std::runtime_error("HDR source exceeds 64 MiB");
    std::vector<uint8_t> bytes(size);
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(size));
    if (!file || !stbi_is_hdr_from_memory(bytes.data(), int(size)))
        throw std::runtime_error("Expected Radiance .hdr image");
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(bytes.data(), int(size), &w, &h, &c) || w < 2 || h < 1 || w != h * 2 ||
        w > 4096)
        throw std::runtime_error("Environment HDR must be 2:1 equirectangular, at most 4096 x 2048");
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf_from_memory(bytes.data(), int(size), &w, &h, &c, 3), stbi_image_free);
    if (!pixels)
        throw std::runtime_error("Cannot decode HDR image");
    HdrImage out{uint32_t(w), uint32_t(h), {pixels.get(), pixels.get() + size_t(w) * h * 3}};
    for (auto v : out.rgb)
        if (!std::isfinite(v) || v < 0 || v > 1e12f)
            throw std::runtime_error("HDR radiance must be finite, nonnegative and at most 1e12");
    return out;
}
ImageFit parseImageFit(const std::string &value) {
    if (value == "contain")
        return ImageFit::Contain;
    if (value == "cover")
        return ImageFit::Cover;
    if (value == "stretch")
        return ImageFit::Stretch;
    throw std::runtime_error("Image fit must be contain, cover or stretch");
}
UiImage importImage(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > MaxImageBytes)
        throw std::runtime_error("Source image must be between 1 byte and 64 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(size));
    if (!file)
        throw std::runtime_error("Cannot read source image");
    int w = 0, h = 0, channels = 0;
    if (stbi_is_hdr_from_memory(bytes.data(), int(size)) ||
        !stbi_info_from_memory(bytes.data(), int(size), &w, &h, &channels) || w < 1 || h < 1 ||
        w > MaxImageDimension || h > MaxImageDimension)
        throw std::runtime_error("Expected a PNG or JPEG image, at most 4096 x 4096");
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), int(size), &w, &h, &channels, 4), stbi_image_free);
    if (!pixels)
        throw std::runtime_error("Cannot decode PNG/JPEG image");
    return {uint32_t(w), uint32_t(h), std::vector<uint8_t>(pixels.get(), pixels.get() + size_t(w) * h * 4)};
}
} // namespace gm
