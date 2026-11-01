#pragma once
#include "render_assets.hpp"
#include "image_import.hpp"
namespace gm {
// irradiance is cosine-convolved radiance / pi; specular mips are GGX prefiltered.
CookedTexture cookEnvironment(const HdrImage &image, bool irradiance, uint32_t width);
} // namespace gm
