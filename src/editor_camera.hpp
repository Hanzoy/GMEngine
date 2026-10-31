#pragma once
#include "render_assets.hpp"
#include <limits>

namespace gm {
inline Vec3 add3D(Vec3 a, Vec3 b, float scale = 1) {
    for (int i = 0; i < 3; ++i)
        a[i] += b[i] * scale;
    return a;
}
inline float dot3D(Vec3 a, Vec3 b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
struct EditorBounds3D {
    Vec3 minimum{}, maximum{};
    bool valid = false;
    void include(Vec3 p) {
        if (!valid) {
            minimum = maximum = p;
            valid = true;
            return;
        }
        for (int i = 0; i < 3; ++i) {
            minimum[i] = std::min(minimum[i], p[i]);
            maximum[i] = std::max(maximum[i], p[i]);
        }
    }
    void include(const CookedMesh &mesh, const Mat4 &world) {
        for (int i = 0; i < 8; ++i)
            include(transformPoint(world, {(i & 1) ? mesh.maximum[0] : mesh.minimum[0],
                                           (i & 2) ? mesh.maximum[1] : mesh.minimum[1],
                                           (i & 4) ? mesh.maximum[2] : mesh.minimum[2]}));
    }
    Vec3 center() const {
        return {(minimum[0] + maximum[0]) * .5f, (minimum[1] + maximum[1]) * .5f,
                (minimum[2] + maximum[2]) * .5f};
    }
};

// Editor-only state. +Z forward, +Y up; pitch is positive when looking down.
struct EditorCamera {
    Vec3 position{6, 4, -8};
    float yaw = -35, pitch = 20, distance = 10, speed = 5;
    static constexpr float radians = .0174532925199433f;
    Mat4 matrix() const {
        sdk::Transform3D t;
        t.x = position[0];
        t.y = position[1];
        t.z = position[2];
        t.rotationX = pitch;
        t.rotationY = yaw;
        return local3D(t);
    }
    Vec3 right() const {
        auto m = matrix();
        return {m[0], m[1], m[2]};
    }
    Vec3 up() const {
        auto m = matrix();
        return {m[4], m[5], m[6]};
    }
    Vec3 forward() const {
        auto m = matrix();
        return {m[8], m[9], m[10]};
    }
    Vec3 pivot() const {
        return add3D(position, forward(), distance);
    }
    ViewCamera3D view() const {
        ViewCamera3D result;
        result.transform = matrix();
        result.lens.nearPlane = .01f;
        result.lens.farPlane = 100000;
        return result;
    }
    bool valid() const {
        return std::all_of(position.begin(), position.end(),
                           [](float v) { return std::isfinite(v) && std::abs(v) <= 1e7f; }) &&
               std::isfinite(yaw) && std::abs(yaw) <= 360 && std::isfinite(pitch) &&
               std::abs(pitch) <= 89.5f && std::isfinite(distance) && distance >= .02f &&
               distance <= 100000 && std::isfinite(speed) && speed >= .01f && speed <= 10000;
    }
    void fromGameCamera(const Mat4 &m) {
        position = {m[12], m[13], m[14]};
        auto f = normalize3D({m[8], m[9], m[10]});
        yaw = std::atan2(f[0], f[2]) / radians;
        pitch = std::clamp(-std::asin(std::clamp(f[1], -1.f, 1.f)) / radians, -89.5f, 89.5f);
    }
    void look(float dx, float dy, bool orbit = false) {
        auto target = pivot();
        yaw = std::remainder(yaw + dx * .2f, 360.f);
        pitch = std::clamp(pitch + dy * .2f, -89.5f, 89.5f);
        if (orbit)
            position = add3D(target, forward(), -distance);
    }
    void pan(float dx, float dy, float viewportHeight) {
        const float scale = 2 * distance * std::tan(30 * radians) / std::max(1.f, viewportHeight);
        position = add3D(add3D(position, right(), -dx * scale), up(), dy * scale);
    }
    void dolly(float steps) {
        const float next =
            std::clamp(distance * std::exp(std::clamp(-steps * .14f, -20.f, 20.f)), .02f, 100000.f);
        position = add3D(position, forward(), distance - next);
        distance = next;
    }
    void changeSpeed(float steps) {
        speed = std::clamp(speed * std::exp(std::clamp(steps * .14f, -20.f, 20.f)), .01f, 10000.f);
    }
    void fly(float x, float y, float z, float seconds, bool fast = false) {
        auto move = add3D(add3D(Vec3{0, y, 0}, right(), x), forward(), z);
        float length = std::sqrt(dot3D(move, move));
        if (length > 0)
            position =
                add3D(position, move,
                      speed * (fast ? 4.f : 1.f) * std::clamp(seconds, 0.f, .1f) / std::max(1.f, length));
    }
    void frame(const EditorBounds3D &bounds, float aspect) {
        if (!bounds.valid)
            return;
        auto half = add3D(bounds.maximum, bounds.minimum, -1);
        float radius = std::max(.25f, std::sqrt(dot3D(half, half)) * .5f);
        float halfAngle = std::min(30 * radians, std::atan(std::tan(30 * radians) * std::max(.01f, aspect)));
        distance = std::clamp(radius / std::sin(halfAngle) * 1.1f, .02f, 100000.f);
        position = add3D(bounds.center(), forward(), -distance);
    }
    Vec3 ray(float x, float y, float aspect) const {
        auto local =
            Vec3{(x * 2 - 1) * aspect * std::tan(30 * radians), (1 - y * 2) * std::tan(30 * radians), 1};
        auto m = matrix();
        m[12] = m[13] = m[14] = 0;
        return normalize3D(transformPoint(m, local));
    }
};

// Triangle picking, preserving world-ray t under nonuniform scale. No physics World required.
inline uint32_t pickEditorMesh(const RenderAssets &assets, const RenderFrame &frame,
                               const EditorCamera &camera, float x, float y, float aspect) {
    auto direction = camera.ray(x, y, aspect);
    float closest = camera.view().lens.farPlane;
    uint32_t selected = UINT32_MAX;
    for (const auto &draw : frame.meshes) {
        const auto &mesh = assets.meshes.at(draw.mesh);
        auto inverse = inverseAffine3D(draw.transform);
        auto origin = transformPoint(inverse, camera.position);
        auto ray = add3D(transformPoint(inverse, add3D(camera.position, direction)), origin, -1);
        float entry = 0, exit = closest;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(ray[a]) < 1e-9f) {
                if (origin[a] < mesh.minimum[a] || origin[a] > mesh.maximum[a])
                    exit = -1;
            } else {
                float lo = (mesh.minimum[a] - origin[a]) / ray[a],
                      hi = (mesh.maximum[a] - origin[a]) / ray[a];
                if (lo > hi)
                    std::swap(lo, hi);
                entry = std::max(entry, lo);
                exit = std::min(exit, hi);
            }
        }
        if (entry > exit)
            continue;
        auto point = [&](uint32_t i) {
            const auto &v = mesh.vertices[i];
            return Vec3{v.x, v.y, v.z};
        };
        auto cull = draw.material == UINT32_MAX ? CullMode::Back : assets.materials.at(draw.material).cull;
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            auto a = point(mesh.indices[i]), e1 = add3D(point(mesh.indices[i + 1]), a, -1),
                 e2 = add3D(point(mesh.indices[i + 2]), a, -1);
            auto p = cross3D(ray, e2);
            float det = dot3D(e1, p);
            if (std::abs(det) < 1e-9f || (cull == CullMode::Back && det < 0) ||
                (cull == CullMode::Front && det > 0))
                continue;
            auto t = add3D(origin, a, -1);
            float u = dot3D(t, p) / det;
            if (u < 0 || u > 1)
                continue;
            auto q = cross3D(t, e1);
            float v = dot3D(ray, q) / det;
            if (v < 0 || u + v > 1)
                continue;
            float depth = dot3D(e2, q) / det;
            if (depth >= camera.view().lens.nearPlane && depth < closest) {
                closest = depth;
                selected = draw.entity;
            }
        }
    }
    return selected;
}
} // namespace gm
