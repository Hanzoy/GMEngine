#pragma once
#include "renderer.hpp"
#include "document.hpp"
namespace gm {
class AuthoringImages {
public:
    explicit AuthoringImages(Renderer& renderer):renderer_(renderer) {}
    ~AuthoringImages() {clear();}
    void clear();
    void draw(const Json& node,Rect box,float scale,const std::filesystem::path& directory);
    void sprite(const Json& node,const std::array<float,8>& points,Rect uv,const std::filesystem::path& directory);
    std::string description(const std::filesystem::path& path) const;
private:
    struct Entry {bgfx::TextureHandle handle=BGFX_INVALID_HANDLE;std::string error;uint32_t width=0,height=0;};
    Renderer& renderer_;
    uint64_t bytes_=0;
    std::unordered_map<std::filesystem::path,Entry> cache_;
    std::unordered_map<std::string,std::filesystem::path> references_;
    Entry& load(const std::string& source,const std::filesystem::path& directory);
};
}
