#include "ui_runtime.hpp"
#include <algorithm>
namespace gm {
void UiRuntime::render(const Json& node,float ox,float oy,float scale,Rect parentClip,const UiHooks& hooks) {
    if(!node.value("visible",true)) return;
    float x=node["x"].get<float>(),y=node["y"].get<float>();
    if(hooks.transform) hooks.transform(node,x,y);
    Rect r{ox+x*scale,oy+y*scale,node["w"].get<float>()*scale,node["h"].get<float>()*scale};
    const float left=std::max(r.x,parentClip.x),top=std::max(r.y,parentClip.y);
    Rect clip{left,top,std::max(0.f,std::min(r.x+r.w,parentClip.x+parentClip.w)-left),std::max(0.f,std::min(r.y+r.h,parentClip.y+parentClip.h)-top)};
    if(clip.w<1 || clip.h<1) return;
    renderer_.clip(clip);
    const auto kind=node["type"].get<std::string>();
    if(kind!="label" && kind!="image") renderer_.rect(r,color(node.value("color",std::string("#182538"))));
    if(kind=="button" || kind=="label") {
        const auto caption=node.value("text",std::string()); const float font=node.value("fontSize",14.f)*scale;
        const float pad=kind=="button"?std::max(6*scale,(r.w-renderer_.textWidth(caption,font))*.5f):0;
        renderer_.text(r.x+pad,r.y+(kind=="button"?std::max(0.f,(r.h-font)*.5f-2*scale):0),caption,font,color(node.value("textColor",std::string("#DFE7F2"))),r.w-pad-3*scale);
    }
    if(hooks.observe) hooks.observe(node,clip);
    if(kind!="panel" && kind!="button" && kind!="label" && hooks.paintCustom) hooks.paintCustom(node,r,scale);
    if(node.contains("children")) for(const auto& child:node["children"]) render(child,r.x,r.y,scale,clip,hooks);
    renderer_.clip(parentClip);
}
}
