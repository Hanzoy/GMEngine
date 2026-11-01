#pragma once
#include "image_types.hpp"
#include "content_hash.hpp"
#include <bgfx/bgfx.h>
#include <unordered_map>
#include <map>
#include <vector>
#include <array>
#include <chrono>
#include <functional>
namespace gm {
uint32_t color(const std::string &hex);
std::wstring wide(const std::string &utf8);
std::string utf8(const std::wstring &text);
struct ImageHandle {
    uint64_t id = 0;
};
inline bool valid(ImageHandle h) {
    return h.id != 0;
}
class Renderer {
  public:
    struct Stats {
        uint32_t quads = 0, drawCalls = 0, textUploads = 0, imageUploads = 0, textEvictions = 0,
                 textCacheEntries = 0, textAtlasPages = 0;
        uint32_t imageCacheHits = 0, imageEntries = 0, imageTextures = 0, imageAtlasPages = 0,
                 imageEvictions = 0;
        uint32_t geometryBuilds = 0, cachedDraws = 0, geometryEntries = 0, totalSprites = 0,
                 spriteCandidates = 0, visibleSprites = 0, submittedSprites = 0, visibleBlocks = 0;
        uint64_t textTextureBytes = 0, imageTextureBytes = 0, uploadBytes = 0, vertexUploadBytes = 0,
                 geometryBytes = 0, frame = 0;
        double frontendMs = 0, presentWaitMs = 0, renderCpuMs = -1, gpuMs = -1, waitRenderMs = -1,
               waitSubmitMs = -1;
        int64_t gpuMemoryBytes = -1, bgfxTextureBytes = -1;
        uint32_t gpuFrame = 0, backendDraws = 0;
        uint32_t totalMeshes=0, visibleMeshes=0, meshDraws=0, instances=0, triangles=0, postPasses=0, compressedTextures=0, mipLevels=0;
        uint64_t meshBytes=0, materialTextureBytes=0, targetBytes=0, lightingBytes=0;
        uint32_t shadowDraws=0,rsmDraws=0,lightingTriangles=0,environmentDraws=0,lightingCacheHits=0;
        bool hdr=false;
        std::string backend;
    };
    Renderer(void *window, uint32_t width, uint32_t height, bgfx::CallbackI *callback = nullptr, bool vsync = true);
    ~Renderer();
    void begin(uint32_t width, uint32_t height);
    void end();
    void rect(Rect r, uint32_t rgba);
    void border(Rect r, uint32_t rgba, float thickness = 1);
    void text(float x, float y, const std::string &text, float size, uint32_t rgba, float maxWidth = 10000);
    float textWidth(const std::string &text, float size);
    void clip(Rect r);
    void unclip();
    ImageHandle uploadImage(const UiImage &image);
    void releaseImage(ImageHandle image);
    void image(Rect box, ImageHandle image, ImageFit fit, uint32_t tint = 0xffffffff);
    void sprite(const std::array<float, 8> &points, ImageHandle image, uint32_t tint, Rect uv = {0, 0, 1, 1});
    size_t imageTextureCount() const;
    // Persistent ordered geometry. Cache IDs become invalid after eviction; callers rebuild.
    uint64_t buildGeometry(const std::function<void()> &build);
    bool drawGeometry(uint64_t id, const float *transform = nullptr, const Rect *scissor = nullptr);
    void releaseGeometry(uint64_t id);
    bool geometryValid(uint64_t id) const;
    void visibility(uint32_t total, uint32_t candidates, uint32_t visible, uint32_t submitted,
                    uint32_t blocks);
    void trimImages(bool allUnused = false);
    const Stats &stats() const {
        return completed_;
    }

    // Ordered GPU layers share one bgfx frame with editor chrome and retained UI.
    uint16_t nextView();
    uint8_t clearColor(const float* color);
    void layer2D(bgfx::FrameBufferHandle target, Rect logical, uint16_t w, uint16_t h, bool linear);
    void backbuffer();
    Stats& frameStats(){return stats_;}
  private:
    uint16_t activeView_=0, nextView_=1, layerWidth_=0, layerHeight_=0;
    Rect layerBounds_{};
    bool linearOutput_=false;
    uint8_t nextPalette_=0;
    struct Texture {
        bgfx::TextureHandle handle;
        int w, h;
        uint64_t lastUsed = 0;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        bool pooled = false;
    };
    struct TextPage {
        bgfx::TextureHandle handle;
        int x = 1, y = 1, rowHeight = 0;
        uint64_t lastUsed = 0;
    };
    struct NumberAtlas {
        Texture texture;
        int cellWidth;
        std::array<int, 14> advances;
    };
    struct Image {
        Texture texture;
        ContentHash hash;
        uint32_t references = 1;
        uint64_t lastUsed = 0;
    };
    struct ImagePage {
        bgfx::TextureHandle handle;
        int x = 0, y = 0, rowHeight = 0;
    };
    struct Vertex {
        float x, y, u, v;
        uint32_t color;
    };
    struct GeometryBatch {
        bgfx::VertexBufferHandle vb;
        bgfx::IndexBufferHandle ib;
        bgfx::TextureHandle texture;
        uint32_t quads = 0;
        uint64_t bytes = 0;
    };
    struct Geometry {
        std::vector<GeometryBatch> batches;
        uint64_t epoch = 0, lastUsed = 0;
    };
    void quad(Rect r, bgfx::TextureHandle texture, uint32_t rgba, float u1 = 1, float u0 = 0, float v1 = 1,
              float v0 = 0);
    void spriteRaw(const std::array<float, 8> &points, bgfx::TextureHandle texture, uint32_t tint, Rect uv);
    void flush();
    void submit(bgfx::TextureHandle texture, const float *transform = nullptr, const Rect *scissor = nullptr);
    float textRun(float x, float y, const std::string &text, int size, uint32_t color, float maxWidth);
    float runWidth(const std::string &text, int size);
    void trimTextCache();
    Texture makeText(const std::string &text, int size);
    Texture storeText(const void *pixels, int width, int height);
    NumberAtlas makeNumberAtlas(int size);
    NumberAtlas &numberAtlas(int size);
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE, output_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    uint32_t width_, height_, resetFlags_;
    Rect clipRect_{};
    bool clipEnabled_ = false;
    bgfx::TextureHandle batchTexture_ = BGFX_INVALID_HANDLE;
    std::vector<Vertex> vertices_;
    std::vector<uint16_t> indices_;
    uint32_t batchQuads_ = 0;
    uint64_t frame_ = 0, textureEpoch_ = 1, nextImage_ = 1, imageBytes_ = 0, nextGeometry_ = 1,
             recording_ = 0, geometryBytes_ = 0;
    bool captureOverflow_ = false;
    std::chrono::steady_clock::time_point frameStart_;
    Stats stats_, completed_;
    std::unordered_map<std::string, Texture> textCache_;
    std::unordered_map<int, NumberAtlas> numberAtlases_;
    std::vector<TextPage> textPages_;
    std::map<std::pair<ContentHash, std::pair<uint32_t, uint32_t>>, uint64_t> imageKeys_;
    std::unordered_map<uint64_t, Image> images_;
    std::vector<ImagePage> imagePages_;
    std::unordered_map<uint64_t, Geometry> geometry_;
};
} // namespace gm
