#pragma once
#include "ui_types.hpp"
#include <vector>

namespace gm {
enum class ImageFit:uint32_t {Contain,Cover,Stretch};
inline constexpr uint32_t MaxImageDimension=4096;
inline constexpr uint64_t MaxImageBytes=64ull*1024*1024;
struct UiImage {uint32_t width=0,height=0;std::vector<uint8_t> rgba;};
struct ImagePlacement {Rect bounds;float u0=0,v0=0,u1=1,v1=1;};
inline ImagePlacement placeImage(Rect box,uint32_t width,uint32_t height,ImageFit fit) {
    ImagePlacement p{box};
    if(!width || !height || box.w<=0 || box.h<=0) return p;
    const float source=float(width)/float(height),target=box.w/box.h;
    if(fit==ImageFit::Contain) {
        if(source>target) {p.bounds.h=box.w/source;p.bounds.y+=(box.h-p.bounds.h)*.5f;}
        else {p.bounds.w=box.h*source;p.bounds.x+=(box.w-p.bounds.w)*.5f;}
    } else if(fit==ImageFit::Cover) {
        if(source>target) {const float fraction=target/source;p.u0=(1-fraction)*.5f;p.u1=1-p.u0;}
        else {const float fraction=source/target;p.v0=(1-fraction)*.5f;p.v1=1-p.v0;}
    }
    return p;
}
}
