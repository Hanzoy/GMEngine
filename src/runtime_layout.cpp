#include "runtime_layout.hpp"
#include <stdexcept>
#include <cmath>

namespace gm {
void RuntimeLayout::update(const RuntimeDocument& document,float width,float height) {
    if(document_==&document && revision_==document.layoutRevision() && width_==width && height_==height) return;
    if(width<=0 || height<=0) throw std::runtime_error("Viewport must have positive dimensions");
    const auto& nodes=document.nodes();const auto& root=nodes[0].bounds;
    scale_=std::min(width/root.w,height/root.h);
    const float rootW=nodes[0].fillX?width/scale_:root.w,rootH=nodes[0].fillY?height/scale_:root.h;
    const float ox=(width-rootW*scale_)*.5f-root.x*scale_,oy=(height-rootH*scale_)*.5f-root.y*scale_;
    boxes_.resize(nodes.size());clips_.resize(nodes.size());visible_.clear();buttons_.clear();
    std::vector<float> cursors(nodes.size(),0);std::vector<uint32_t> counts(nodes.size(),0);
    for(uint32_t i=0;i<nodes.size();) {
        const auto& node=nodes[i];if(!document.visible(i)) {i=node.subtreeEnd;continue;}
        const auto& b=node.bounds;
        if(node.parent==NoIndex)boxes_[i]={ox+b.x*scale_,oy+b.y*scale_,rootW*scale_,rootH*scale_};
        else {
            const auto &parent=nodes[node.parent];const auto &p=boxes_[node.parent];const float pad=parent.padding*scale_,gap=parent.gap*scale_;
            const float innerW=std::max(0.f,p.w-2*pad),innerH=std::max(0.f,p.h-2*pad);
            float w=node.fillX?std::max(0.f,innerW-2*b.x*scale_):b.w*scale_,h=node.fillY?std::max(0.f,innerH-2*b.y*scale_):b.h*scale_;
            float x=p.x+pad+b.x*scale_,y=p.y+pad+b.y*scale_;
            auto &cursor=cursors[node.parent];auto &count=counts[node.parent];
            if(parent.layout==UiLayout::Row){x+=cursor;cursor+=w+gap;}
            else if(parent.layout==UiLayout::Column){y+=cursor;cursor+=h+gap;}
            else if(parent.layout==UiLayout::Grid){float cell=std::max(0.f,(innerW-gap*(parent.columns-1))/parent.columns);x+=(count%parent.columns)*(cell+gap);y+=(count/parent.columns)*(parent.rowHeight*scale_+gap);if(node.fillX)w=std::max(0.f,cell-2*b.x*scale_);if(node.fillY)h=std::max(0.f,parent.rowHeight*scale_-2*b.y*scale_);}
            else{x+=node.anchorX*innerW-node.pivotX*w;y+=node.anchorY*innerH-node.pivotY*h;}
            ++count;boxes_[i]={x,y,w,h};
        }
        clips_[i]=intersect(boxes_[i],node.parent==NoIndex?Rect{0,0,width,height}:clips_[node.parent]);
        if(clips_[i].w<1 || clips_[i].h<1) {i=node.subtreeEnd;continue;}
        visible_.push_back(i);if(node.kind==UiKind::Button || node.event) buttons_.push_back(i);++i;
    }
    document_=&document;revision_=document.layoutRevision();width_=width;height_=height;++rebuilds_;
}
uint32_t RuntimeLayout::hit(float x,float y) const {
    for(auto it=buttons_.rbegin();it!=buttons_.rend();++it) if(clips_[*it].contains(x,y)) return *it;
    return NoIndex;
}
uint32_t RuntimeLayout::listAt(const RuntimeDocument& document,float x,float y) const {
    for(auto it=visible_.rbegin();it!=visible_.rend();++it)if(document.nodes()[*it].kind==UiKind::List&&clips_[*it].contains(x,y))return *it;return NoIndex;
}
int32_t RuntimeLayout::listRow(const RuntimeDocument& document,uint32_t node,float y) const {
    if(node>=boxes_.size()||document.nodes()[node].kind!=UiKind::List)return -1;
    const auto &n=document.nodes()[node];const auto &b=boxes_[node];
    float offset=std::min(document.scrollOffset(node),std::max(0.f,float(document.list(node).size())*n.rowHeight-b.h/scale_+2*n.padding));
    float local=(y-b.y)/scale_-n.padding;
    if(local<0||local>=b.h/scale_-2*n.padding)return -1;
    auto row=int32_t(std::floor((local+offset)/n.rowHeight));return row>=0&&uint32_t(row)<document.list(node).size()?row:-1;
}
}
