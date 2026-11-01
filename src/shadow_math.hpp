#pragma once
#include "render_assets.hpp"
namespace gm {
struct ShadowProjection {
    Mat4 view = identity3D(), projection = identity3D(), clip = identity3D();
    Vec3 origin{};
    float extent = 1;
};
// Camera-independent caster depth range includes objects outside the main camera frustum.
// XY translation snaps to shadow texels; fixed extent prevents projection breathing.
inline ShadowProjection shadowProjection(const RenderAssets &assets, const RenderFrame &frame,
                                         const Mat4 &eye, uint32_t resolution, bool homogeneous) {
    ShadowProjection result;
    const auto z = normalize3D({frame.light.directionX, frame.light.directionY, frame.light.directionZ});
    const auto x = normalize3D(cross3D(std::abs(z[1]) > .95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, z));
    const auto y = cross3D(z, x);
    auto dot = [](Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    result.extent = frame.shadows.distance * .5f;
    const float texel = 2 * result.extent / std::max(1u, resolution);
    Vec3 center{eye[12] + eye[8] * result.extent * .5f, eye[13] + eye[9] * result.extent * .5f,
                eye[14] + eye[10] * result.extent * .5f};
    const float cx = std::round(dot(center, x) / texel) * texel,
                cy = std::round(dot(center, y) / texel) * texel;
    float nearZ = 1e30f, farZ = -1e30f;
    const CookedMaterial fallback;
    for (const auto &draw : frame.meshes) {
        const auto &material = draw.material == UINT32_MAX ? fallback : assets.materials.at(draw.material);
        if (material.blend != BlendMode::Opaque || !material.depthWrite)
            continue;
        const auto &mesh = assets.meshes.at(draw.mesh);
        for (int i = 0; i < 8; ++i) {
            auto p = transformPoint(draw.transform, {i & 1 ? mesh.maximum[0] : mesh.minimum[0],
                                                     i & 2 ? mesh.maximum[1] : mesh.minimum[1],
                                                     i & 4 ? mesh.maximum[2] : mesh.minimum[2]});
            nearZ = std::min(nearZ, dot(p, z) - 1);
            farZ = std::max(farZ, dot(p, z) + 1);
        }
    }
    if (nearZ > farZ) {
        nearZ = dot(center, z) - result.extent;
        farZ = nearZ + 2 * result.extent;
    }
    for (int i = 0; i < 3; ++i)
        result.origin[i] = x[i] * cx + y[i] * cy + z[i] * (nearZ + farZ) * .5f;
    result.view = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -cx, -cy, -nearZ, 1};
    result.projection = identity3D();
    result.projection[0] = result.projection[5] = 1 / result.extent;
    result.projection[10] = (homogeneous ? 2.f : 1.f) / (farZ - nearZ);
    result.projection[14] = homogeneous ? -1.f : 0.f;
    result.clip = multiply3D(result.projection, result.view);
    return result;
}
} // namespace gm
