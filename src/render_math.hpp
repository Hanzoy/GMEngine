#pragma once
#include <gm/render.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace gm {
using Mat4 = std::array<float, 16>;
using Vec3 = std::array<float, 3>;
inline Mat4 identity3D() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
inline Mat4 multiply3D(const Mat4 &a, const Mat4 &b) {
    Mat4 c{};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k)
                c[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
    return c;
}
inline Vec3 transformPoint(const Mat4 &a, Vec3 p) {
    return {a[0] * p[0] + a[4] * p[1] + a[8] * p[2] + a[12], a[1] * p[0] + a[5] * p[1] + a[9] * p[2] + a[13],
            a[2] * p[0] + a[6] * p[1] + a[10] * p[2] + a[14]};
}
inline Vec3 normalize3D(Vec3 p) {
    float n = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (n < 1e-12f)
        return {0, 1, 0};
    for (auto &v : p)
        v /= n;
    return p;
}
inline Vec3 cross3D(Vec3 a, Vec3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Mat4 local3D(const sdk::Transform3D &t) {
    constexpr float r = .0174532925199433f;
    float cx = std::cos(t.rotationX * r), sx = std::sin(t.rotationX * r), cy = std::cos(t.rotationY * r),
          sy = std::sin(t.rotationY * r), cz = std::cos(t.rotationZ * r), sz = std::sin(t.rotationZ * r);
    Mat4 x = {1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1},
         y = {cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1},
         z = {cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto out = multiply3D(z, multiply3D(y, x));
    for (int i = 0; i < 3; ++i) {
        out[i] *= t.scaleX;
        out[4 + i] *= t.scaleY;
        out[8 + i] *= t.scaleZ;
    }
    out[12] = t.x;
    out[13] = t.y;
    out[14] = t.z;
    return out;
}
inline Mat4 inverseAffine3D(const Mat4 &m) {
    Vec3 a{m[0], m[1], m[2]}, b{m[4], m[5], m[6]}, c{m[8], m[9], m[10]};
    auto r0 = cross3D(b, c), r1 = cross3D(c, a), r2 = cross3D(a, b);
    float d = a[0] * r0[0] + a[1] * r0[1] + a[2] * r0[2];
    if (!std::isfinite(d) || std::abs(d) < 1e-12f)
        throw std::runtime_error("Singular 3D transform");
    auto out = identity3D();
    for (int i = 0; i < 3; ++i) {
        out[i * 4] = r0[i] / d;
        out[i * 4 + 1] = r1[i] / d;
        out[i * 4 + 2] = r2[i] / d;
    }
    auto p = transformPoint(out, {m[12], m[13], m[14]});
    out[12] = -p[0];
    out[13] = -p[1];
    out[14] = -p[2];
    return out;
}
inline Mat4 perspective3D(float fov, float aspect, float nearZ, float farZ, bool homogeneous) {
    Mat4 m{};
    const float h = 1 / std::tan(fov * .00872664625997165f);
    m[0] = h / aspect;
    m[5] = h;
    m[10] = homogeneous ? (farZ + nearZ) / (farZ - nearZ) : farZ / (farZ - nearZ);
    m[11] = 1;
    m[14] = homogeneous ? -2 * nearZ * farZ / (farZ - nearZ) : -nearZ * farZ / (farZ - nearZ);
    return m;
}
inline bool visibleBounds3D(const Mat4 &clip, Vec3 lo, Vec3 hi, bool homogeneous) {
    uint32_t common = 63;
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? hi[0] : lo[0], y = (i & 2) ? hi[1] : lo[1], z = (i & 4) ? hi[2] : lo[2];
        float p[4]{};
        for (int j = 0; j < 4; ++j)
            p[j] = clip[j] * x + clip[4 + j] * y + clip[8 + j] * z + clip[12 + j];
        uint32_t mask = 0;
        mask |= p[0] < -p[3] ? 1 : 0;
        mask |= p[0] > p[3] ? 2 : 0;
        mask |= p[1] < -p[3] ? 4 : 0;
        mask |= p[1] > p[3] ? 8 : 0;
        mask |= p[2] < (homogeneous ? -p[3] : 0) ? 16 : 0;
        mask |= p[2] > p[3] ? 32 : 0;
        common &= mask;
    }
    return !common;
}
} // namespace gm
