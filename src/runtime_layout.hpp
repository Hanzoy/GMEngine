#pragma once
#include "runtime_document.hpp"

namespace gm {
// Layout and hit regions are rebuilt only after geometry/visibility/viewport changes.
class RuntimeLayout {
public:
    void update(const RuntimeDocument& document,float width,float height);
    uint32_t hit(float x,float y) const;
    uint32_t listAt(const RuntimeDocument& document,float x,float y) const;
    int32_t listRow(const RuntimeDocument& document,uint32_t node,float y) const;
    const std::vector<uint32_t>& buttons() const {return buttons_;}
    const std::vector<Rect>& boxes() const {return boxes_;}
    const std::vector<Rect>& clips() const {return clips_;}
    const std::vector<uint32_t>& visibleNodes() const {return visible_;}
    float scale() const {return scale_;}
    uint64_t rebuilds() const {return rebuilds_;}
private:
    const RuntimeDocument* document_=nullptr;
    uint64_t revision_=0,rebuilds_=0;
    float width_=0,height_=0,scale_=1;
    std::vector<Rect> boxes_,clips_;
    std::vector<uint32_t> visible_,buttons_;
};
}
