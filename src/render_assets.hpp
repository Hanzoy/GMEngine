#pragma once
#include "render_math.hpp"
#include "resource_limits.hpp"
#include <span>
#include <string>
#include <vector>
namespace gm {
enum class TextureFormat : uint32_t { Rgba8, BC3, ASTC4x4, ETC2RGBA, Rgba16F };
struct TextureVariant {
    TextureFormat format = TextureFormat::Rgba8;
    std::vector<uint8_t> bytes;
};
struct CookedTexture {
    uint32_t width = 0, height = 0, levels = 1;
    bool srgb = true, repeat = true, linear = true;
    std::vector<TextureVariant> variants;
};
struct MeshVertex {
    float x = 0, y = 0, z = 0, nx = 0, ny = 1, nz = 0, u = 0, v = 0;
};
struct CookedMesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    Vec3 minimum{}, maximum{};
};
struct ShaderVariant {
    uint32_t backend = 0;
    std::vector<uint8_t> vertex, fragment;
}; // 0 D3D11, 1 GLES3.
struct CookedShader {
    std::vector<ShaderVariant> variants;
};
struct MaterialParameter {
    std::string name;
    std::array<float, 4> value{};
};
struct MaterialSampler {
    std::string name;
    uint32_t texture = UINT32_MAX, stage = 0;
};
enum class BlendMode : uint32_t { Opaque, Alpha, Additive };
enum class CullMode : uint32_t { Back, Front, None };
struct CookedMaterial {
    uint32_t shader = UINT32_MAX;
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    bool depthTest = true, depthWrite = true;
    std::vector<MaterialParameter> parameters;
    std::vector<MaterialSampler> samplers;
};
struct RenderAssets {
    std::vector<CookedTexture> textures;
    std::vector<CookedMesh> meshes;
    std::vector<CookedShader> shaders;
    std::vector<CookedMaterial> materials;
};
struct MeshDraw {
    uint32_t entity = 0, mesh = UINT32_MAX, material = UINT32_MAX;
    Mat4 transform = identity3D();
};
struct RenderFrame {
    std::vector<MeshDraw> meshes;
    Mat4 camera = identity3D();
    sdk::Camera3D lens;
    bool cameraEnabled = false;
    sdk::DirectionalLight light;
    sdk::PostProcess post{0};
    sdk::ShadowSettings shadows{0};
    sdk::EnvironmentLight environment{0};
    sdk::IndirectLight indirect{0};
    sdk::RenderQuality quality{0};
};
// Optional view-owned camera. Never writes to the game World or copies its mesh list.
struct ViewCamera3D {
    Mat4 transform = identity3D();
    sdk::Camera3D lens;
};
uint64_t textureLevelBytes(TextureFormat format, uint32_t width, uint32_t height);
uint64_t textureBytes(const CookedTexture &texture, TextureFormat format);
void validateRenderAssets(const RenderAssets &assets);
std::vector<uint8_t> encodeRenderAssets(const RenderAssets &assets);
RenderAssets decodeRenderAssets(std::span<const uint8_t> bytes);
CookedMesh primitiveMesh(const std::string &name);
void updateMeshBounds(CookedMesh &mesh);
} // namespace gm
