#include "authoring_images.hpp"
#include "image_import.hpp"
#include <cmath>
namespace gm {
void AuthoringImages::clear(){for(auto& [path,entry]:cache_)if(bgfx::isValid(entry.handle))renderer_.releaseImage(entry.handle);cache_.clear();references_.clear();bytes_=0;}
AuthoringImages::Entry& AuthoringImages::load(const std::string& source,const std::filesystem::path& directory){
    if(cache_.size()>=512)clear();auto dir=directory.generic_u8string();auto key=std::string(dir.begin(),dir.end())+"\n"+source;std::filesystem::path path;
    try{auto reference=references_.find(key);if(reference==references_.end())reference=references_.emplace(key,resolveResource(directory,source)).first;path=reference->second;}catch(const std::exception&){auto& missing=cache_[directory/".missing-asset"];missing.error="图片 ID 无法解析 · 请刷新资源";return missing;}
    auto it=cache_.find(path);if(it!=cache_.end())return it->second;auto& entry=cache_[path];
    try{if(source.empty())throw std::runtime_error("No image");auto image=importImage(path);if(bytes_+image.rgba.size()>MaxImageBytes)throw std::runtime_error("Image cache full");entry.handle=renderer_.uploadImage(image);entry.width=image.width;entry.height=image.height;bytes_+=image.rgba.size();}catch(const std::exception&){entry.error=source.empty()?"请选择 PNG / JPEG 图片":"图片无法读取 · 检查路径后刷新资源";}return entry;
}
void AuthoringImages::draw(const Json& node,Rect box,float scale,const std::filesystem::path& directory){auto& entry=load(node.value("source",std::string()),directory);if(bgfx::isValid(entry.handle)){const auto tint=(color(node.value("tint",std::string("#FFFFFF")))&0xffffff)|(uint32_t(std::lround(node.value("opacity",1.f)*255))<<24);renderer_.image(box,entry.handle,parseImageFit(node.value("fit",std::string("contain"))),tint);}else{renderer_.rect(box,color("#222B36"));renderer_.border(box,color("#60758E"));renderer_.text(box.x+8*scale,box.y+8*scale,entry.error,12*scale,color("#BDCADA"),box.w-16*scale);}}
void AuthoringImages::sprite(const Json& node,const std::array<float,8>& points,Rect uv,const std::filesystem::path& directory){auto& entry=load(node.value("source",std::string()),directory);renderer_.sprite(points,entry.handle,color(bgfx::isValid(entry.handle)?node.value("tint",std::string("#FFFFFF")):"#A04B65"),uv);}
std::string AuthoringImages::description(const std::filesystem::path& path)const{const auto it=cache_.find(std::filesystem::absolute(path).lexically_normal());if(it==cache_.end())return "";if(!it->second.error.empty())return it->second.error;return std::to_string(it->second.width)+" × "+std::to_string(it->second.height)+" px · PNG / JPEG";}
}
