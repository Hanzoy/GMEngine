#include "runtime_document.hpp"
#include <stdexcept>
#include <cmath>

namespace gm {
void RuntimeDocument::setBoundText(uint32_t index,const std::string& text) {
    if(index>=asset_.nodes.size()||text.size()>2048)throw std::runtime_error("Invalid text binding");auto& node=asset_.nodes[index];if(node.text==text)return;
    node.text=text;node.integerText=parseCounter(text,node.integerValue);++paintRevision_;
}
void RuntimeDocument::setBoundWidth(uint32_t index,float width) {
    if(index==0||index>=asset_.nodes.size()||!std::isfinite(width)||width<0||width>16384)throw std::runtime_error("Invalid width binding");
    auto& node=asset_.nodes[index];bool visible=width>0;float next=std::max(1.f,width);if(node.bounds.w==next&&node.visible==visible)return;
    node.bounds.w=next;node.visible=visible;updateVisibility(index,node.subtreeEnd);++layoutRevision_;++paintRevision_;
}
RuntimeDocument::RuntimeDocument(std::filesystem::path path):path_(std::move(path)) {reload();}
RuntimeDocument::RuntimeDocument(CompiledUi asset):asset_(std::move(asset)) {
    validateCompiledUi(asset_);visibility_.resize(asset_.nodes.size());updateVisibility(0,uint32_t(asset_.nodes.size()));
}
void RuntimeDocument::reload() {
    if(path_.empty()) throw std::runtime_error("Runtime instance has no resource path");
    const auto time=std::filesystem::last_write_time(path_);
    auto next=loadUi(path_);std::vector<uint8_t> visible(next.nodes.size());
    asset_=std::move(next);visibility_=std::move(visible);updateVisibility(0,uint32_t(asset_.nodes.size()));++layoutRevision_;++paintRevision_;++assetRevision_;
    sourceTime_=time;pixelsReleased_=false;
}
void RuntimeDocument::releaseImagePixels() {
    if(path_.empty() || asset_.images.empty() || pixelsReleased_) return;
    for(auto& image:asset_.images) std::vector<uint8_t>().swap(image.rgba);
    pixelsReleased_=true;
}
void RuntimeDocument::restoreImagePixels() {
    if(!pixelsReleased_) return;
    if(std::filesystem::last_write_time(path_)!=sourceTime_) throw std::runtime_error("Resource changed; reload before recreating image textures");
    auto next=loadUi(path_);
    if(next.images.size()!=asset_.images.size()) throw std::runtime_error("Image resource changed; reload required");
    for(size_t i=0;i<next.images.size();++i) if(next.images[i].width!=asset_.images[i].width || next.images[i].height!=asset_.images[i].height) throw std::runtime_error("Image dimensions changed; reload required");
    asset_.images=std::move(next.images);pixelsReleased_=false;
}
void RuntimeDocument::updateVisibility(uint32_t first,uint32_t end) {
    for(uint32_t i=first;i<end;++i) {const auto& node=asset_.nodes[i];visibility_[i]=uint8_t(node.visible && (node.parent==NoIndex || visibility_[node.parent]));}
}
ActionResult RuntimeDocument::activate(uint32_t index) {
    if(index>=asset_.nodes.size()) return ActionResult::NoAction;
    if(!visible(index)) return ActionResult::Hidden;
    auto actionIndex=asset_.nodes[index].action;if(actionIndex==NoIndex) return ActionResult::NoAction;
    const auto& action=asset_.actions[actionIndex];auto& target=asset_.nodes[action.target];
    switch(action.op) {
        case UiOp::AddInteger: {
            if(!target.integerText) return ActionResult::InvalidCounter;
            auto value=target.integerValue+action.integer;
            if(value<-1000000000 || value>1000000000) return ActionResult::OutOfRange;
            auto text=std::to_string(value);target.text=std::move(text);target.integerValue=value;break;
        }
        case UiOp::SetText:target.text=action.text;target.integerText=action.packed!=0;target.integerValue=action.integer;break;
        case UiOp::SetColor:target.color=action.packed;break;
        case UiOp::SetTextColor:target.textColor=action.packed;break;
        case UiOp::SetVisible:target.visible=action.packed!=0;updateVisibility(action.target,target.subtreeEnd);++layoutRevision_;break;
        case UiOp::SetX:target.bounds.x=action.number;++layoutRevision_;break;
        case UiOp::SetY:target.bounds.y=action.number;++layoutRevision_;break;
        case UiOp::SetWidth:target.bounds.w=action.number;++layoutRevision_;break;
        case UiOp::SetHeight:target.bounds.h=action.number;++layoutRevision_;break;
        case UiOp::SetFontSize:target.fontSize=action.number;break;
        case UiOp::SetImageFit:target.fit=ImageFit(action.packed);break;
        case UiOp::SetTint:target.tint=(target.tint&0xff000000)|(action.packed&0xffffff);break;
        case UiOp::SetOpacity:target.tint=(target.tint&0xffffff)|(uint32_t(std::lround(action.number*255))<<24);break;
    }
    ++paintRevision_;return ActionResult::Applied;
}
}
