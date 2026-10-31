#include "editor_icons.hpp"
#include "editor_icon_sources.hpp"
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#include <memory>
#include <cmath>
#include <stdexcept>
namespace gm {
std::vector<std::string_view> editorIconNames() {
    std::vector<std::string_view> names;
    for (const auto &source : kEditorIconSources)
        names.push_back(source.name);
    return names;
}
UiImage rasterEditorIcon(std::string_view name, int pixels) {
    if (pixels < 8 || pixels > 128)
        throw std::runtime_error("Editor icon size must be 8..128 pixels");
    const EditorIconSource *source = nullptr;
    for (const auto &entry : kEditorIconSources)
        if (entry.name == name) {
            source = &entry;
            break;
        }
    if (!source)
        throw std::runtime_error("Unknown editor icon: " + std::string(name));
    std::vector<char> text(source->svg.begin(), source->svg.end());
    text.push_back(0);
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> svg(nsvgParse(text.data(), "px", 96), nsvgDelete);
    if (!svg || !svg->shapes || svg->width != 24 || svg->height != 24)
        throw std::runtime_error("Invalid editor SVG");
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> raster(nsvgCreateRasterizer(),
                                                                            nsvgDeleteRasterizer);
    if (!raster)
        throw std::bad_alloc();
    UiImage image{uint32_t(pixels), uint32_t(pixels), std::vector<uint8_t>(size_t(pixels) * pixels * 4)};
    nsvgRasterize(raster.get(), svg.get(), 0, 0, float(pixels) / 24, image.rgba.data(), pixels, pixels,
                  pixels * 4);
    return image;
}
void EditorIcons::draw(std::string_view name, Rect box, uint32_t tint) {
    const auto pixels = std::clamp(int(std::lround(std::max(box.w, box.h))), 8, 128);
    auto key = std::pair{std::string(name), pixels};
    auto it = cache_.find(key);
    if (it == cache_.end())
        it = cache_.emplace(std::move(key), renderer_.uploadImage(rasterEditorIcon(name, pixels))).first;
    renderer_.image(box, it->second, ImageFit::Contain, tint);
}
void EditorIcons::clear() {
    for (auto &[key, handle] : cache_)
        renderer_.releaseImage(handle);
    cache_.clear();
}
} // namespace gm
