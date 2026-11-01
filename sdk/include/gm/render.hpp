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
// Optional scene-wide lighting components. Absence keeps the legacy lighting path.
struct ShadowSettings {
    static constexpr const char *gm_name = "ShadowSettings";
    uint32_t enabled = 1;
    int32_t resolution = 2048;
    float distance = 60, depthBias = .0005f, normalBias = .02f, softness = 1;
};
struct EnvironmentLight {
    static constexpr const char *gm_name = "EnvironmentLight";
    uint32_t enabled = 1, irradiance = UINT32_MAX, radiance = UINT32_MAX;
    float intensity = 1, rotation = 0;
    uint32_t background = 1;
};
// Reflective shadow map: dynamic, one-bounce diffuse transport from the directional light.
struct IndirectLight {
    static constexpr const char *gm_name = "IndirectLight";
    uint32_t enabled = 1;
    int32_t resolution = 256, samples = 32;
    float radius = 5, intensity = 1;
};
// Optional forward-renderer quality settings. Absent components preserve existing scenes.
struct RenderQuality {
    static constexpr const char *gm_name = "RenderQuality";
    uint32_t enabled = 1, temporalAA = 1, ambientOcclusion = 1;
    float historyWeight = .9f, aoRadius = 1.f, aoIntensity = 1.5f, aoBias = .05f;
};
// Forward point lights. The renderer selects up to eight affecting lights per mesh.
// Intensity is linear radiant intensity; local light shadows are not implemented.
struct PointLight {
    static constexpr const char *gm_name = "PointLight";
    uint32_t color = 0xffffffff;
    float intensity = 20, range = 10;
    uint32_t enabled = 1;
};
struct HeightFog {
    static constexpr const char *gm_name = "HeightFog";
    uint32_t color = 0xff665544;
    float density = .02f, heightFalloff = .12f, baseHeight = 0, startDistance = 0, maxOpacity = .95f;
    uint32_t enabled = 1;
};
// One horizontal reflection plane per viewport. Materials opt in via u_planar.x.
struct PlanarReflection {
    static constexpr const char *gm_name = "PlanarReflection";
    float height=0, resolutionScale=.5f, strength=1;
    uint32_t enabled=1;
};
} // namespace gm::sdk
