#include "environment_cooker.hpp"
#include <bit>
#include <cstring>
namespace gm {
namespace {
constexpr float Pi = 3.14159265358979323846f;
float radical(uint32_t i) {
    i = (i << 16) | (i >> 16);
    i = ((i & 0x55555555) << 1) | ((i & 0xaaaaaaaa) >> 1);
    i = ((i & 0x33333333) << 2) | ((i & 0xcccccccc) >> 2);
    i = ((i & 0x0f0f0f0f) << 4) | ((i & 0xf0f0f0f0) >> 4);
    i = ((i & 0x00ff00ff) << 8) | ((i & 0xff00ff00) >> 8);
    return float(i) * 2.3283064365386963e-10f;
}
uint16_t half(float value) {
    // Positive radiance; round to nearest, preserve subnormals and clamp overflow.
    uint32_t b = std::bit_cast<uint32_t>(std::clamp(value, 0.f, 65504.f)), e = (b >> 23) & 255,
             m = b & 0x7fffff;
    if (e < 103)
        return 0;
    if (e < 113) {
        m |= 0x800000;
        uint32_t shift = 126 - e;
        return uint16_t((m + (1u << (shift - 1))) >> shift);
    }
    return uint16_t(std::min(0x7bffu, ((e - 112) << 10) + ((m + 0x1000) >> 13)));
}
Vec3 direction(float u, float v) {
    float phi = (u - .5f) * 2 * Pi, theta = v * Pi;
    return {std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta)};
}
Vec3 sample(const HdrImage &im, Vec3 d) {
    float u = std::atan2(d[2], d[0]) / (2 * Pi) + .5f, v = std::acos(std::clamp(d[1], -1.f, 1.f)) / Pi;
    float x = u * im.width - .5f, y = v * im.height - .5f;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    Vec3 result{};
    for (int b = 0; b < 2; ++b)
        for (int a = 0; a < 2; ++a) {
            auto px = (ix + a + int(im.width)) % int(im.width),
                 py = std::clamp(iy + b, 0, int(im.height) - 1);
            float weight = (a ? fx : 1 - fx) * (b ? fy : 1 - fy);
            for (int k = 0; k < 3; ++k)
                result[k] += im.rgb[(size_t(py) * im.width + px) * 3 + k] * weight;
        }
    return result;
}
// Solid-angle weighted source mips are essential for small, very bright lights (e.g. the sun).
// Point-sampling the original HDR at every roughness creates bright Monte Carlo speckles.
HdrImage downsample(const HdrImage &source) {
    HdrImage result{std::max(1u, source.width / 2), std::max(1u, source.height / 2), {}};
    result.rgb.resize(size_t(result.width) * result.height * 3);
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x) {
            double left = double(x) * source.width / result.width,
                   right = double(x + 1) * source.width / result.width;
            double top = double(y) * source.height / result.height,
                   bottom = double(y + 1) * source.height / result.height;
            double sum[3]{}, weight = 0;
            for (int sy = int(top); sy < int(std::ceil(bottom)); ++sy)
                for (int sx = int(left); sx < int(std::ceil(right)); ++sx) {
                    double w = (std::min(right, double(sx + 1)) - std::max(left, double(sx))) *
                               (std::cos(std::max(top, double(sy)) * Pi / source.height) -
                                std::cos(std::min(bottom, double(sy + 1)) * Pi / source.height));
                    for (int c = 0; c < 3; ++c)
                        sum[c] += source.rgb[(size_t(sy) * source.width + sx) * 3 + c] * w;
                    weight += w;
                }
            for (int c = 0; c < 3; ++c)
                result.rgb[(size_t(y) * result.width + x) * 3 + c] = float(sum[c] / weight);
        }
    return result;
}
std::array<double, 9> basis(Vec3 n) {
    double x = n[0], y = n[1], z = n[2];
    return {.28209479177387814,
            .4886025119029199 * x,
            .4886025119029199 * y,
            .4886025119029199 * z,
            1.0925484305920792 * x * y,
            1.0925484305920792 * y * z,
            .31539156525252005 * (3 * z * z - 1),
            1.0925484305920792 * x * z,
            .5462742152960396 * (x * x - y * y)};
}
using Harmonics = std::array<std::array<double, 3>, 9>;
Harmonics irradianceSh(const HdrImage &image) {
    Harmonics coefficients{};
    std::array<double, 9> integral{};
    double area = 0;
    for (uint32_t y = 0; y < image.height; ++y) {
        double weight =
            2 * double(Pi) / image.width *
            (std::cos(double(y) * Pi / image.height) - std::cos(double(y + 1) * Pi / image.height));
        for (uint32_t x = 0; x < image.width; ++x) {
            auto sh = basis(direction((x + .5f) / image.width, (y + .5f) / image.height));
            for (int i = 0; i < 9; ++i) {
                integral[i] += sh[i] * weight;
                for (int c = 0; c < 3; ++c)
                    coefficients[i][c] += sh[i] * weight * image.rgb[(size_t(y) * image.width + x) * 3 + c];
            }
            area += weight;
        }
    }
    // Remove the discretization residual of a constant signal from higher bands.
    for (int c = 0; c < 3; ++c) {
        double mean = coefficients[0][c] / integral[0];
        for (int i = 1; i < 9; ++i)
            coefficients[i][c] -= mean * integral[i];
    }
    for (int i = 0; i < 9; ++i)
        for (auto &value : coefficients[i])
            value *= ((i == 0 ? 1. : i < 4 ? 2. / 3 : 1. / 4) * 4 * double(Pi) / area);
    return coefficients;
}
} // namespace
CookedTexture cookEnvironment(const HdrImage &im, bool irradiance, uint32_t width) {
    if (im.width < 2 || im.height * 2 != im.width || im.rgb.size() != size_t(im.width) * im.height * 3 ||
        width < 4 || width > 1024 || (width & (width - 1)))
        throw std::runtime_error("Invalid HDR environment size (power of two 4..1024)");
    for (float v : im.rgb)
        if (!std::isfinite(v) || v < 0 || v > 65504)
            throw std::runtime_error("Invalid HDR radiance");
    CookedTexture out;
    out.width = width;
    out.height = width / 2;
    out.srgb = false;
    out.repeat = false;
    out.linear = true;
    if (!irradiance)
        for (auto n = width; n > 1; n /= 2)
            ++out.levels;
    TextureVariant variant{TextureFormat::Rgba16F, {}};
    variant.bytes.reserve(size_t(textureBytes(out, TextureFormat::Rgba16F)));
    uint32_t w = out.width, h = out.height;
    Harmonics sh{};
    std::vector<HdrImage> sourceMips;
    if (irradiance)
        sh = irradianceSh(im);
    else {
        sourceMips.push_back(im);
        while (sourceMips.back().width > 1 || sourceMips.back().height > 1)
            sourceMips.push_back(downsample(sourceMips.back()));
    }
    auto filtered = [&](Vec3 d, float lod) {
        lod = std::clamp(lod, 0.f, float(sourceMips.size() - 1));
        auto level = uint32_t(lod);
        float blend = lod - level;
        auto a = sample(sourceMips[level], d),
             b = sample(sourceMips[std::min(level + 1, uint32_t(sourceMips.size() - 1))], d);
        for (int c = 0; c < 3; ++c)
            a[c] = a[c] * (1 - blend) + b[c] * blend;
        return a;
    };
    // Deterministic Hammersley integration. Compile-time only, no sampling work in Player.
    for (uint32_t level = 0; level < out.levels; ++level) {
        float rough = float(level) / std::max(1u, out.levels - 1), alpha = std::max(.001f, rough * rough);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                auto n = direction((x + .5f) / w, (y + .5f) / h);
                Vec3 rgb{};
                if (irradiance) {
                    auto harmonics = basis(n);
                    for (int c = 0; c < 3; ++c) {
                        double value = 0;
                        for (int i = 0; i < 9; ++i)
                            value += sh[i][c] * harmonics[i];
                        rgb[c] = float(std::max(0., value));
                    }
                } else if (level == 0)
                    rgb = filtered(n, std::log2(float(im.width) / width));
                else {
                    auto tangent =
                             normalize3D(cross3D(std::abs(n[1]) > .99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, n)),
                         bitangent = cross3D(n, tangent);
                    float weight = 0;
                    uint32_t samples = irradiance ? 1024 : 256;
                    for (uint32_t i = 0; i < samples; ++i) {
                        float u = (i + .5f) / samples, v = radical(i), phi = 2 * Pi * v;
                        float cosTheta = irradiance ? std::sqrt(1 - u)
                                                    : std::sqrt((1 - u) / (1 + (alpha * alpha - 1) * u));
                        float sinTheta = std::sqrt(std::max(0.f, 1 - cosTheta * cosTheta));
                        Vec3 d{};
                        for (int k = 0; k < 3; ++k)
                            d[k] = tangent[k] * std::cos(phi) * sinTheta +
                                   bitangent[k] * std::sin(phi) * sinTheta + n[k] * cosTheta;
                        float nl = 1;
                        if (!irradiance) {
                            for (int k = 0; k < 3; ++k)
                                d[k] = 2 * cosTheta * d[k] - n[k];
                            nl = std::max(0.f, 2 * cosTheta * cosTheta - 1);
                        }
                        if (nl > 0) {
                            float denominator = cosTheta * cosTheta * (alpha * alpha - 1) + 1;
                            float pdf = alpha * alpha / (4 * Pi * denominator * denominator);
                            float texelArea =
                                2 * Pi * Pi *
                                std::max(1.f / im.height, std::sqrt(std::max(0.f, 1 - d[1] * d[1]))) /
                                (float(im.width) * im.height);
                            float lod = .5f * std::log2(1.f / (samples * pdf * texelArea));
                            auto s = filtered(d, lod);
                            for (int k = 0; k < 3; ++k)
                                rgb[k] += s[k] * nl;
                            weight += nl;
                        }
                    }
                    for (auto &v : rgb)
                        v /= std::max(weight, .0001f);
                }
                for (float v : {rgb[0], rgb[1], rgb[2], 1.f}) {
                    auto b = half(v);
                    variant.bytes.push_back(uint8_t(b));
                    variant.bytes.push_back(uint8_t(b >> 8));
                }
            }
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    out.variants.push_back(std::move(variant));
    return out;
}
} // namespace gm
