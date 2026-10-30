#include "runtime_document.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace gm {
uint32_t RuntimeDocument::find(const std::string& id) const {
    for(uint32_t i=0;i<asset_.nodes.size();++i)if(asset_.nodes[i].id==id)return i;
    throw std::runtime_error("Unknown UI node: "+id);
}
void RuntimeDocument::bindText(const std::string& name,const std::string& text) {
    bool found=false;
    for(uint32_t i=0;i<asset_.nodes.size();++i)if(asset_.nodes[i].bindText==name && !name.empty()){setBoundText(i,text);found=true;}
    if(!found)setBoundText(find(name),text);
}
void RuntimeDocument::setVisible(uint32_t index,bool visible) {
    auto &node=asset_.nodes.at(index);if(node.visible==visible)return;
    node.visible=visible;updateVisibility(index,node.subtreeEnd);++layoutRevision_;++paintRevision_;
}
void RuntimeDocument::setList(uint32_t index,const char* const* rows,uint32_t count) {
    if(asset_.nodes.at(index).kind!=UiKind::List || count>10000 || (count&&!rows))throw std::runtime_error("Invalid UI list");
    std::vector<std::string> next;next.reserve(count);size_t total=0;
    for(uint32_t i=0;i<count;++i){if(!rows[i])throw std::runtime_error("Null UI row");std::string row(rows[i]);validateUiText(row);total+=row.size();if(total>2*1024*1024)throw std::runtime_error("UI list text budget exceeded");next.push_back(std::move(row));}
    if(list(index)==next)return;size_t oldBytes=0;for(const auto &row:list(index))oldBytes+=row.size();
    auto bytes=listBytes_-oldBytes+total,rowsCount=listRows_-list(index).size()+next.size();
    if(bytes>8*1024*1024||rowsCount>100000)throw std::runtime_error("Document list budget exceeded");
    lists_[index]=std::move(next);listBytes_=bytes;listRows_=rowsCount;scroll(index,0);++paintRevision_;
}
const std::vector<std::string>& RuntimeDocument::list(uint32_t index) const {
    static const std::vector<std::string> empty;
    auto i=lists_.find(index);return i==lists_.end()?empty:i->second;
}
float RuntimeDocument::scrollOffset(uint32_t index) const {auto i=scroll_.find(index);return i==scroll_.end()?0:i->second;}
void RuntimeDocument::scroll(uint32_t index,float pixels) {
    const auto &node=asset_.nodes.at(index);
    if(!std::isfinite(pixels)||node.kind!=UiKind::List)throw std::runtime_error("Scroll requires a list and finite pixels");
    auto next=std::clamp(scrollOffset(index)+pixels,0.f,std::max(0.f,float(list(index).size())*node.rowHeight-node.bounds.h+2*node.padding));
    if(next!=scrollOffset(index)){scroll_[index]=next;++paintRevision_;}
}
void RuntimeDocument::setBoundText(uint32_t index,const std::string& text) {
    if(index>=asset_.nodes.size())throw std::runtime_error("Invalid text binding");validateUiText(text);auto& node=asset_.nodes[index];if(node.text==text)return;
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
    asset_=std::move(next);visibility_=std::move(visible);lists_.clear();scroll_.clear();listBytes_=listRows_=0;updateVisibility(0,uint32_t(asset_.nodes.size()));++layoutRevision_;++paintRevision_;++assetRevision_;
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
