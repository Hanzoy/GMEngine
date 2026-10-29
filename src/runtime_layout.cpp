#include "runtime_layout.hpp"
#include <stdexcept>

namespace gm {
void RuntimeLayout::update(const RuntimeDocument& document,float width,float height) {
    if(document_==&document && revision_==document.layoutRevision() && width_==width && height_==height) return;
    if(width<=0 || height<=0) throw std::runtime_error("Viewport must have positive dimensions");
    const auto& nodes=document.nodes();const auto& root=nodes[0].bounds;
    scale_=std::min(width/root.w,height/root.h);
    const float ox=(width-root.w*scale_)*.5f-root.x*scale_,oy=(height-root.h*scale_)*.5f-root.y*scale_;
    boxes_.resize(nodes.size());clips_.resize(nodes.size());visible_.clear();buttons_.clear();
    for(uint32_t i=0;i<nodes.size();) {
        const auto& node=nodes[i];if(!document.visible(i)) {i=node.subtreeEnd;continue;}
        const auto& b=node.bounds;
        const float x=node.parent==NoIndex?ox:boxes_[node.parent].x,y=node.parent==NoIndex?oy:boxes_[node.parent].y;
        boxes_[i]={x+b.x*scale_,y+b.y*scale_,b.w*scale_,b.h*scale_};
        clips_[i]=intersect(boxes_[i],node.parent==NoIndex?Rect{0,0,width,height}:clips_[node.parent]);
        if(clips_[i].w<1 || clips_[i].h<1) {i=node.subtreeEnd;continue;}
        visible_.push_back(i);if(node.action!=NoIndex) buttons_.push_back(i);++i;
    }
    document_=&document;revision_=document.layoutRevision();width_=width;height_=height;++rebuilds_;
}
uint32_t RuntimeLayout::hit(float x,float y) const {
    for(auto it=buttons_.rbegin();it!=buttons_.rend();++it) if(clips_[*it].contains(x,y)) return *it;
    return NoIndex;
}
}
