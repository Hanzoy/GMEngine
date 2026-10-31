#include "render_compiler.hpp"
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <cstring>
#include <set>
namespace gm {
namespace {
float toLinear(float x) {
    return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
float toSrgb(float x) {
    return x <= .0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - .055f;
}
uint8_t byte(float x) {
    return uint8_t(std::clamp(std::lround(x * 255), 0l, 255l));
}
} // namespace
std::vector<UiImage> generateMipmaps(const UiImage &image, bool srgb, bool normalMap) {
    if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
        image.rgba.size() != uint64_t(image.width) * image.height * 4)
        throw std::runtime_error("Invalid mipmap source");
    std::vector<UiImage> result{image};
    while (result.back().width > 1 || result.back().height > 1) {
        const auto &src = result.back();
        UiImage dst{std::max(1u, src.width / 2), std::max(1u, src.height / 2), {}};
        dst.rgba.resize(size_t(dst.width) * dst.height * 4);
        for (uint32_t y = 0; y < dst.height; ++y)
            for (uint32_t x = 0; x < dst.width; ++x) {
                double x0 = double(x) * src.width / dst.width, x1 = double(x + 1) * src.width / dst.width,
                       y0 = double(y) * src.height / dst.height, y1 = double(y + 1) * src.height / dst.height;
                float sum[4]{}, weight = 0;
                for (uint32_t sy = uint32_t(y0); sy < std::min(src.height, uint32_t(std::ceil(y1))); ++sy)
                    for (uint32_t sx = uint32_t(x0); sx < std::min(src.width, uint32_t(std::ceil(x1)));
                         ++sx) {
                        float w = float((std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                                        (std::min(y1, double(sy + 1)) - std::max(y0, double(sy))));
                        auto *p = src.rgba.data() + (size_t(sy) * src.width + sx) * 4;
                        float alpha = p[3] / 255.f;
                        for (int c = 0; c < 3; ++c) {
                            float v = p[c] / 255.f;
                            if (normalMap)
                                v = v * 2 - 1;
                            else {
                                if (srgb)
                                    v = toLinear(v);
                                v *= alpha;
                            }
                            sum[c] += v * w;
                        }
                        sum[3] += alpha * w;
                        weight += w;
                    }
                auto *p = dst.rgba.data() + (size_t(y) * dst.width + x) * 4;
                if (normalMap) {
                    auto n = normalize3D({sum[0], sum[1], sum[2]});
                    for (int c = 0; c < 3; ++c)
                        p[c] = byte(n[c] * .5f + .5f);
                } else
                    for (int c = 0; c < 3; ++c) {
                        float value = sum[3] > 1e-10f ? sum[c] / sum[3] : 0;
                        if (srgb)
                            value = toSrgb(value);
                        p[c] = byte(value);
                    }
                p[3] = byte(sum[3] / weight);
            }
        result.push_back(std::move(dst));
    }
    return result;
}
CookedTexture cookTexture(const UiImage &image, const TextureSettings &settings) {
    if (settings.formats.empty() || settings.formats.size() > 4 ||
        std::set<TextureFormat>(settings.formats.begin(), settings.formats.end()).size() !=
            settings.formats.size())
        throw std::runtime_error("Texture needs at least one output format");
    auto mips = generateMipmaps(image, settings.srgb && !settings.normalMap, settings.normalMap);
    if (!settings.mipmaps)
        mips.resize(1);
    CookedTexture out{image.width,
                      image.height,
                      uint32_t(mips.size()),
                      settings.srgb && !settings.normalMap,
                      settings.repeat,
                      settings.linear,
                      {}};
    bx::DefaultAllocator allocator;
    for (auto format : settings.formats) {
        TextureVariant variant{format, {}};
        variant.bytes.reserve(size_t(textureBytes(out, format)));
        for (const auto &mip : mips) {
            if (format == TextureFormat::Rgba8) {
                variant.bytes.insert(variant.bytes.end(), mip.rgba.begin(), mip.rgba.end());
                continue;
            }
            auto w = (mip.width + 3) / 4 * 4, h = (mip.height + 3) / 4 * 4;
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                    std::memcpy(pixels.data() + (size_t(y) * w + x) * 4,
                                mip.rgba.data() + (size_t(std::min(y, mip.height - 1)) * mip.width +
                                                   std::min(x, mip.width - 1)) *
                                                      4,
                                4);
            auto offset = variant.bytes.size();
            variant.bytes.resize(offset + size_t(textureLevelBytes(format, mip.width, mip.height)));
            bimg::TextureFormat::Enum target;
            switch (format) {
            case TextureFormat::BC3:
                target = bimg::TextureFormat::BC3;
                break;
            case TextureFormat::ASTC4x4:
                target = bimg::TextureFormat::ASTC4x4;
                break;
            case TextureFormat::ETC2RGBA:
                target = bimg::TextureFormat::ETC2A;
                break;
            default:
                throw std::runtime_error("Unknown texture compression");
            }
            bx::Error error;
            bimg::imageEncodeFromRgba8(
                &allocator, variant.bytes.data() + offset, pixels.data(), w, h, 1, target,
                settings.normalMap ? bimg::Quality::NormalMapFastest : bimg::Quality::Fastest, &error);
            if (!error.isOk())
                throw std::runtime_error("Texture compression failed");
        }
        out.variants.push_back(std::move(variant));
    }
    RenderAssets test;
    test.textures.push_back(out);
    validateRenderAssets(test);
    return out;
}
} // namespace gm
