#include "image_import.hpp"
#include <fstream>
#include <memory>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#ifdef _MSC_VER
#pragma warning(push,0)
#endif
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace gm {
ImageFit parseImageFit(const std::string& value) {
    if(value=="contain") return ImageFit::Contain;
    if(value=="cover") return ImageFit::Cover;
    if(value=="stretch") return ImageFit::Stretch;
    throw std::runtime_error("Image fit must be contain, cover or stretch");
}
UiImage importImage(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(!size || size>MaxImageBytes) throw std::runtime_error("Source image must be between 1 byte and 64 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));std::ifstream file(path,std::ios::binary);
    file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
    if(!file) throw std::runtime_error("Cannot read source image");
    int w=0,h=0,channels=0;
    if(!stbi_info_from_memory(bytes.data(),int(size),&w,&h,&channels) || w<1 || h<1 || w>MaxImageDimension || h>MaxImageDimension)
        throw std::runtime_error("Expected a PNG or JPEG image, at most 4096 x 4096");
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(bytes.data(),int(size),&w,&h,&channels,4),stbi_image_free);
    if(!pixels) throw std::runtime_error("Cannot decode PNG/JPEG image");
    return {uint32_t(w),uint32_t(h),std::vector<uint8_t>(pixels.get(),pixels.get()+size_t(w)*h*4)};
}
}
