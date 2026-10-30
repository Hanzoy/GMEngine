#pragma once
#include "compiled_ui.hpp"
#include <unordered_map>

namespace gm {
enum class ActionResult {Applied,NoAction,Hidden,OutOfRange,InvalidCounter};
class RuntimeDocument {
public:
    explicit RuntimeDocument(std::filesystem::path path);
    explicit RuntimeDocument(CompiledUi asset);
    const std::vector<UiNode>& nodes() const {return asset_.nodes;}
    const std::vector<UiImage>& images() const {return asset_.images;}
    uint64_t assetRevision() const {return assetRevision_;}
    bool visible(uint32_t node) const {return node<visibility_.size() && visibility_[node]!=0;}
    ActionResult activate(uint32_t node);
    void setBoundText(uint32_t node,const std::string& text);
    void setBoundWidth(uint32_t node,float width);
    uint32_t find(const std::string& id) const;
    void bindText(const std::string& name,const std::string& text);
    void setVisible(uint32_t node,bool visible);
    void setList(uint32_t node,const char* const* rows,uint32_t count);
    const std::vector<std::string>& list(uint32_t node) const;
    void scroll(uint32_t node,float pixels);
    float scrollOffset(uint32_t node) const;
    uint64_t layoutRevision() const {return layoutRevision_;}
    uint64_t paintRevision() const {return paintRevision_;}
    void reload();
    // File-backed instances can discard CPU pixels once their textures are uploaded.
    // In-memory assets retain them because they have no reload source.
    void releaseImagePixels();
    void restoreImagePixels();
private:
    void updateVisibility(uint32_t first,uint32_t end);
    std::filesystem::path path_;
    CompiledUi asset_;
    std::vector<uint8_t> visibility_;
    std::unordered_map<uint32_t,std::vector<std::string>> lists_;
    std::unordered_map<uint32_t,float> scroll_;
    size_t listBytes_=0,listRows_=0;
    uint64_t layoutRevision_=1,paintRevision_=1,assetRevision_=1;
    bool pixelsReleased_=false;
    std::filesystem::file_time_type sourceTime_{};
};
}
