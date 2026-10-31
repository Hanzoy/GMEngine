#pragma once
#include "game.hpp"
namespace gm::sdk {
// Left-handed coordinates: +Y up, +Z forward. Euler degrees, local scale then XYZ rotation.
struct Transform3D {
    static constexpr const char *gm_name = "Transform3D";
    float x = 0, y = 0, z = 0, rotationX = 0, rotationY = 0, rotationZ = 0, scaleX = 1, scaleY = 1,
          scaleZ = 1;
};
struct WorldTransform3D {
    static constexpr const char *gm_name = "gm.WorldTransform3D";
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct MeshRenderer {
    static constexpr const char *gm_name = "MeshRenderer";
    uint32_t mesh = UINT32_MAX, material = UINT32_MAX, enabled = 1;
};
struct Camera3D {
    static constexpr const char *gm_name = "Camera3D";
    float fieldOfView = 60, nearPlane = .1f, farPlane = 1000;
    uint32_t enabled = 1;
    int32_t priority = 0;
};
struct DirectionalLight {
    static constexpr const char *gm_name = "DirectionalLight";
    float directionX = -.4f, directionY = -1, directionZ = .3f;
    uint32_t color = 0xffffffff;
    float intensity = 3, ambient = .08f;
    uint32_t enabled = 1;
};
struct PostProcess {
    static constexpr const char *gm_name = "PostProcess";
    uint32_t enabled = 1;
    float exposure = 1, gamma = 2.2f;
    uint32_t toneMapping = 1, bloom = 0;
    float bloomThreshold = 1, bloomIntensity = .15f, bloomRadius = 1;
    uint32_t fxaa = 1;
    float renderScale = 1;
};
} // namespace gm::sdk
