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
    if(kind=="button" || kind=="label" || kind=="list") {
        const auto caption=kind=="list"?std::string("列表 · 运行时绑定数据"):node.value("text",std::string()); const float font=node.value("fontSize",14.f)*scale;
        const float pad=kind=="button"?std::max(6*scale,(r.w-renderer_.textWidth(caption,font))*.5f):0;
        renderer_.text(r.x+pad,r.y+(kind=="button"?std::max(0.f,(r.h-font)*.5f-2*scale):0),caption,font,color(node.value("textColor",std::string("#DFE7F2"))),r.w-pad-3*scale);
    }
    if(hooks.observe) hooks.observe(node,clip);
    if(kind!="panel" && kind!="button" && kind!="label" && hooks.paintCustom) hooks.paintCustom(node,r,scale);
    if(node.contains("children")) {
        float cursor=0;uint32_t count=0;const auto layout=node.value("layout",std::string("absolute"));const float pad=node.value("padding",0.f),gap=node.value("gap",0.f);
        const float innerW=std::max(0.f,r.w/scale-2*pad),innerH=std::max(0.f,r.h/scale-2*pad);const uint32_t columns=node.value("columns",1u);
        for(const auto& source:node["children"]){
            if(!source.value("visible",true))continue;auto child=source;
            float x=child["x"].get<float>(),y=child["y"].get<float>(),w=child["w"].get<float>(),h=child["h"].get<float>();
            if(child.value("fillX",false))w=std::max(0.f,innerW-2*x);if(child.value("fillY",false))h=std::max(0.f,innerH-2*y);
            x+=pad;y+=pad;
            if(layout=="row"){x+=cursor;cursor+=w+gap;}else if(layout=="column"){y+=cursor;cursor+=h+gap;}
            else if(layout=="grid"){float cell=std::max(0.f,(innerW-gap*(columns-1))/columns);x+=(count%columns)*(cell+gap);y+=(count/columns)*(node.value("rowHeight",28.f)+gap);if(child.value("fillX",false))w=std::max(0.f,cell-2*source["x"].get<float>());if(child.value("fillY",false))h=std::max(0.f,node.value("rowHeight",28.f)-2*source["y"].get<float>());}
            else{x+=child.value("anchorX",0.f)*innerW-child.value("pivotX",0.f)*w;y+=child.value("anchorY",0.f)*innerH-child.value("pivotY",0.f)*h;}
            child["x"]=x;child["y"]=y;child["w"]=w;child["h"]=h;++count;render(child,r.x,r.y,scale,clip,hooks);
        }
    }
    renderer_.clip(parentClip);
}
}
