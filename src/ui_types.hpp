#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace gm {
struct Rect {
    float x{},y{},w{},h{};
    bool contains(float px,float py) const {return px>=x && py>=y && px<x+w && py<y+h;}
};
inline Rect intersect(Rect a,Rect b) {
    float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,std::max(0.f,std::min(a.x+a.w,b.x+b.w)-x),std::max(0.f,std::min(a.y+a.h,b.y+b.h)-y)};
}
uint32_t color(const std::string& hex);
std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& text);
}
