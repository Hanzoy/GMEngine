#pragma once
#include "render_assets.hpp"
#include "image_types.hpp"
#include "document.hpp"
#include <filesystem>
#include <map>
#include <set>
namespace gm {
struct TextureSettings {
    bool mipmaps = true, srgb = true, normalMap = false, repeat = true, linear = true;
    std::vector<TextureFormat> formats{TextureFormat::BC3, TextureFormat::ASTC4x4, TextureFormat::ETC2RGBA,
                                       TextureFormat::Rgba8};
};
std::vector<UiImage> generateMipmaps(const UiImage &image, bool srgb, bool normalMap);
CookedTexture cookTexture(const UiImage &image, const TextureSettings &settings);
CookedMesh importObj(const std::filesystem::path &path);
class RenderCompiler {
  public:
    explicit RenderCompiler(RenderAssets &assets);
    uint32_t mesh(const std::filesystem::path &directory, const std::string &reference);
    uint32_t material(const std::filesystem::path &directory, const std::string &reference);
    CookedMaterial materialSource(const Json &source, const std::filesystem::path &directory);
    uint32_t texture(const std::filesystem::path &directory, const std::string &reference,
                     bool normal = false);
    const std::set<std::filesystem::path> &dependencies() const {
        return dependencies_;
    }

  private:
    std::filesystem::path resolve(const std::filesystem::path &directory, const std::string &reference);
    uint32_t shader(const std::filesystem::path &directory, const std::string &reference);
    void addBytes(uint64_t bytes);
    uint64_t payloadBytes_ = 0;
    RenderAssets &assets_;
    std::map<std::string, uint32_t> textures_, meshes_, materials_, shaders_;
    std::set<std::filesystem::path> dependencies_;
};
// Pure source validation/normalization is shared by GUI and CLI material editing.
void targetRenderAssets(RenderAssets &assets, const std::string &platform);
Json normalizedMaterial(const Json &source);
void renderDependencies(const std::filesystem::path &directory, const std::string &reference,
                        std::set<std::filesystem::path> &output, uint32_t depth = 0);
} // namespace gm
