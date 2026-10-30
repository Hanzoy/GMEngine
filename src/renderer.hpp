#pragma once
#include "image_types.hpp"
#include <bgfx/bgfx.h>
#include <unordered_map>
#include <vector>
#include <array>

namespace gm {
uint32_t color(const std::string& hex);
std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& text);
class Renderer {
public:
    struct Stats {uint32_t quads=0,drawCalls=0,textUploads=0,imageUploads=0,textEvictions=0,textCacheEntries=0,textAtlasPages=0;uint64_t textTextureBytes=0;};
    Renderer(void* window, uint32_t width, uint32_t height, bgfx::CallbackI* callback=nullptr);
    ~Renderer();
    void begin(uint32_t width, uint32_t height);
    void end();
    void rect(Rect r, uint32_t rgba);
    void border(Rect r, uint32_t rgba, float thickness=1);
    void text(float x, float y, const std::string& text, float size, uint32_t rgba, float maxWidth=10000);
    float textWidth(const std::string& text, float size);
    void clip(Rect r);
    void unclip();
    bgfx::TextureHandle uploadImage(const UiImage& image);
    void releaseImage(bgfx::TextureHandle image);
    void image(Rect box,bgfx::TextureHandle image,ImageFit fit,uint32_t tint=0xffffffff);
    void sprite(const std::array<float,8>& points,bgfx::TextureHandle image,uint32_t tint,Rect uv={0,0,1,1});
    size_t imageTextureCount() const {return images_.size();}
    const Stats& stats() const {return stats_;}
private:
    struct Texture { bgfx::TextureHandle handle; int w, h; uint64_t lastUsed=0; float u0=0,v0=0,u1=1,v1=1;bool pooled=false; };
    struct TextPage {bgfx::TextureHandle handle;int x=1,y=1,rowHeight=0;uint64_t lastUsed=0;};
    struct NumberAtlas {Texture texture;int cellWidth;std::array<int,14> advances;};
    struct Vertex {float x,y,u,v;uint32_t color;};
    void quad(Rect r, bgfx::TextureHandle texture, uint32_t rgba, float u1=1,float u0=0,float v1=1,float v0=0);
    void flush();
    void textRun(float x,float y,const std::string& text,int size,uint32_t color,float maxWidth);
    float runWidth(const std::string& text,int size);
    void trimTextCache();
    Texture makeText(const std::string& text, int size);
    Texture storeText(const void* pixels,int width,int height);
    NumberAtlas makeNumberAtlas(int size);
    NumberAtlas& numberAtlas(int size);
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE, output_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    uint32_t width_,height_;
    Rect clipRect_{};bool clipEnabled_=false;
    bgfx::TextureHandle batchTexture_=BGFX_INVALID_HANDLE;
    std::vector<Vertex> vertices_;
    std::vector<uint16_t> indices_;
    uint64_t frame_=0;
    Stats stats_;
    std::unordered_map<std::string,Texture> textCache_;
    std::unordered_map<int,NumberAtlas> numberAtlases_;
    std::vector<TextPage> textPages_;
    std::unordered_map<uint16_t,Texture> images_;
};
}
