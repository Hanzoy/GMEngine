#include "authoring_images.hpp"
#include "image_import.hpp"
#include <cmath>
namespace gm {
void AuthoringImages::clear() {
    for(auto& [path,entry]:cache_) if(bgfx::isValid(entry.handle)) renderer_.releaseImage(entry.handle);
    cache_.clear();bytes_=0;
}
void AuthoringImages::draw(const Json& node,Rect box,float scale,const std::filesystem::path& directory) {
    const auto source=node.value("source",std::string());
    const auto path=std::filesystem::absolute(directory/std::filesystem::path(std::u8string(source.begin(),source.end()))).lexically_normal();
    auto it=cache_.find(path);
    if(it==cache_.end()) {
        // Bound the editor cache even while users repeatedly replace image paths.
        if(cache_.size()>=512) clear();
        auto& entry=cache_[path];
        try {
            if(source.empty()) throw std::runtime_error("请选择图片");
            auto image=importImage(path);
            if(bytes_+image.rgba.size()>MaxImageBytes) throw std::runtime_error("图片缓存已达 64 MiB，请刷新资源");
            entry.handle=renderer_.uploadImage(image);entry.width=image.width;entry.height=image.height;bytes_+=image.rgba.size();
        } catch(const std::exception&) {entry.error=source.empty()?"请选择 PNG / JPEG 图片":"图片无法读取 · 检查路径后刷新资源";}
        it=cache_.find(path);
    }
    if(bgfx::isValid(it->second.handle)) {
        const auto tint=(color(node.value("tint",std::string("#FFFFFF")))&0xffffff)|(uint32_t(std::lround(node.value("opacity",1.f)*255))<<24);
        renderer_.image(box,it->second.handle,parseImageFit(node.value("fit",std::string("contain"))),tint);
    } else {
        renderer_.rect(box,color("#222B36"));renderer_.border(box,color("#60758E"));
        renderer_.text(box.x+8*scale,box.y+8*scale,it->second.error,12*scale,color("#BDCADA"),box.w-16*scale);
    }
}
std::string AuthoringImages::description(const std::filesystem::path& path) const {
    const auto it=cache_.find(std::filesystem::absolute(path).lexically_normal());
    if(it==cache_.end()) return "";
    if(!it->second.error.empty()) return it->second.error;
    return std::to_string(it->second.width)+" × "+std::to_string(it->second.height)+" px · PNG / JPEG";
}
}
