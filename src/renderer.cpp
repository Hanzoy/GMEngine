#include "renderer.hpp"
#include <windows.h>
#include <bx/math.h>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <bgfx/embedded_shader.h>
#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

namespace gm {
namespace {
const bgfx::EmbeddedShader shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui), BGFX_EMBEDDED_SHADER(fs_ocornut_imgui), BGFX_EMBEDDED_SHADER_END()
};
bool numeric(const std::string& text) {return !text.empty() && text.size()<=32 && text.find_first_not_of("0123456789-+")==std::string::npos;}
int glyphIndex(char ch) {return ch=='-'?10:ch=='+'?11:ch-'0';}
}
uint32_t color(const std::string& hex) {
    const auto v=static_cast<uint32_t>(std::stoul(hex.substr(1),nullptr,16));
    return ((v>>16)&255) | (v&0xff00) | ((v&255)<<16) | 0xff000000;
}
std::wstring wide(const std::string& text) {
    if(text.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n); return out;
}
std::string utf8(const std::wstring& text) {
    if(text.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr); return out;
}
Renderer::Renderer(void* window,uint32_t width,uint32_t height,bgfx::CallbackI* callback):width_(width),height_(height) {
    bgfx::Init init; init.type=bgfx::RendererType::Direct3D11;
    init.callback=callback;
    init.swapChain.nwh=window; init.swapChain.width=width; init.swapChain.height=height; init.reset=BGFX_RESET_VSYNC;
    if(!bgfx::init(init)) throw std::runtime_error("bgfx D3D11 initialization failed");
    layout_.begin().add(bgfx::Attrib::Position,2,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true).end();
    program_=bgfx::createProgram(bgfx::createEmbeddedShader(shaders,init.type,"vs_ocornut_imgui"),bgfx::createEmbeddedShader(shaders,init.type,"fs_ocornut_imgui"),true);
    if(!bgfx::isValid(program_)) { bgfx::shutdown(); throw std::runtime_error("Cannot create UI shader program"); }
    sampler_=bgfx::createUniform("s_tex",bgfx::UniformType::Sampler);
    output_=bgfx::createUniform("u_imguiOutput",bgfx::UniformType::Vec4);
    const uint32_t pixel=0xffffffff;
    white_=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&pixel,4));
    bgfx::setViewMode(0,bgfx::ViewMode::Sequential);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x090d14ff);
    bgfx::setViewName(0,"GM retained UI");
}
Renderer::~Renderer() {
    for(auto& [key,t] : images_) bgfx::destroy(t.handle);
    for(auto& [key,t] : textCache_) if(!t.pooled) bgfx::destroy(t.handle);
    for(auto& page:textPages_) bgfx::destroy(page.handle);
    for(auto& [key,a] : numberAtlases_) bgfx::destroy(a.texture.handle);
    bgfx::destroy(white_); bgfx::destroy(program_); bgfx::destroy(sampler_); bgfx::destroy(output_); bgfx::shutdown();
}
void Renderer::begin(uint32_t w,uint32_t h) {
    stats_={};++frame_;
    if(w!=width_ || h!=height_) { width_=w; height_=h; bgfx::SwapChain chain; chain.width=w; chain.height=h; bgfx::reset(BGFX_RESET_VSYNC,&chain); }
    if(textCache_.size()>1024) {
        int removed=0;for(auto it=textCache_.begin();it!=textCache_.end() && removed<32;) {
            if(frame_-it->second.lastUsed>120) {if(!it->second.pooled) bgfx::destroy(it->second.handle);it=textCache_.erase(it);++removed;}else ++it;
        }
    }
    float ortho[16]; bx::mtxOrtho(ortho,0,float(w),float(h),0,0,1000,0,bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewRect(0,0,0,uint16_t(w),uint16_t(h)); bgfx::setViewTransform(0,nullptr,ortho); bgfx::touch(0); unclip();
}
void Renderer::end() { flush();bgfx::frame(); }
void Renderer::clip(Rect r) {
    clipRect_=r;clipEnabled_=true;
}
void Renderer::unclip() {clipEnabled_=false;}
void Renderer::quad(Rect r,bgfx::TextureHandle texture,uint32_t c,float u1,float u0,float v1,float v0) {
    if(r.w<=0 || r.h<=0) return;
    // All UI primitives are axis-aligned quads. Clip geometry and UV together so
    // a different widget clip no longer forces a draw-call boundary.
    if(clipEnabled_) {
        const auto clipped=intersect(r,clipRect_);if(clipped.w<=0 || clipped.h<=0) return;
        const float du=u1-u0,dv=v1-v0,oldU=u0,oldV=v0;
        u0=oldU+du*(clipped.x-r.x)/r.w;u1=oldU+du*(clipped.x+clipped.w-r.x)/r.w;
        v0=oldV+dv*(clipped.y-r.y)/r.h;v1=oldV+dv*(clipped.y+clipped.h-r.y)/r.h;r=clipped;
    }
    if(texture.idx!=batchTexture_.idx || vertices_.size()>=4096) {flush();batchTexture_=texture;}
    auto offset=uint16_t(vertices_.size());
    vertices_.insert(vertices_.end(),{{r.x,r.y,u0,v0,c},{r.x+r.w,r.y,u1,v0,c},{r.x+r.w,r.y+r.h,u1,v1,c},{r.x,r.y+r.h,u0,v1,c}});
    for(auto i:{0,1,2,0,2,3}) indices_.push_back(uint16_t(offset+i));++stats_.quads;
}
void Renderer::flush() {
    if(vertices_.empty()) return;
    const auto vcount=uint32_t(vertices_.size()),icount=uint32_t(indices_.size());
    if(bgfx::getAvailTransientVertexBuffer(vcount,layout_)<vcount || bgfx::getAvailTransientIndexBuffer(icount)<icount) throw std::runtime_error("UI transient buffer capacity exceeded");
    bgfx::TransientVertexBuffer vb; bgfx::TransientIndexBuffer ib;
    bgfx::allocTransientVertexBuffer(&vb,vcount,layout_); bgfx::allocTransientIndexBuffer(&ib,icount);
    memcpy(vb.data,vertices_.data(),vertices_.size()*sizeof(Vertex));memcpy(ib.data,indices_.data(),indices_.size()*sizeof(uint16_t));
    const float output[]={0,1,0,0}; bgfx::setUniform(output_,output);
    bgfx::setVertexBuffer(0,&vb); bgfx::setIndexBuffer(&ib); bgfx::setTexture(0,sampler_,batchTexture_);
    bgfx::setScissor(UINT16_MAX);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(0,program_);
    ++stats_.drawCalls;vertices_.clear();indices_.clear();
}
void Renderer::rect(Rect r,uint32_t c) { quad(r,white_,c); }
bgfx::TextureHandle Renderer::uploadImage(const UiImage& image) {
    if(!image.width || !image.height || image.width>MaxImageDimension || image.height>MaxImageDimension || image.rgba.size()!=uint64_t(image.width)*image.height*4) throw std::runtime_error("Invalid image upload");
    auto handle=bgfx::createTexture2D(uint16_t(image.width),uint16_t(image.height),false,1,bgfx::TextureFormat::RGBA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(image.rgba.data(),uint32_t(image.rgba.size())));
    if(!bgfx::isValid(handle)) throw std::runtime_error("Cannot allocate image texture");
    try {images_.emplace(handle.idx,Texture{handle,int(image.width),int(image.height),frame_});}
    catch(...) {bgfx::destroy(handle);throw;}
    ++stats_.imageUploads;return handle;
}
void Renderer::releaseImage(bgfx::TextureHandle handle) {
    if(auto it=images_.find(handle.idx);it!=images_.end()) {flush();bgfx::destroy(handle);images_.erase(it);}
}
void Renderer::image(Rect box,bgfx::TextureHandle handle,ImageFit fit,uint32_t tint) {
    const auto it=images_.find(handle.idx);if(it==images_.end() || (tint>>24)==0) return;
    const auto p=placeImage(box,uint32_t(it->second.w),uint32_t(it->second.h),fit);
    quad(p.bounds,handle,tint,p.u1,p.u0,p.v1,p.v0);
}
void Renderer::border(Rect r,uint32_t c,float t) { rect({r.x,r.y,r.w,t},c);rect({r.x,r.y+r.h-t,r.w,t},c);rect({r.x,r.y,t,r.h},c);rect({r.x+r.w-t,r.y,t,r.h},c); }
void Renderer::sprite(const std::array<float,8>& points,bgfx::TextureHandle texture,uint32_t tint,Rect uv){
    if(!bgfx::isValid(texture))texture=white_;if(!(tint>>24))return;
    std::array<Vertex,12> polygon{},scratch{};size_t count=4;
    polygon[0]={points[0],points[1],uv.x,uv.y,tint};polygon[1]={points[2],points[3],uv.x+uv.w,uv.y,tint};polygon[2]={points[4],points[5],uv.x+uv.w,uv.y+uv.h,tint};polygon[3]={points[6],points[7],uv.x,uv.y+uv.h,tint};
    // Clip rotated quads in CPU space; retain batching and interpolate UVs.
    if(clipEnabled_)for(int side=0;side<4&&count;++side){size_t output=0;float edge=side==0?clipRect_.x:side==1?clipRect_.x+clipRect_.w:side==2?clipRect_.y:clipRect_.y+clipRect_.h;auto coordinate=[&](const Vertex& v){return side<2?v.x:v.y;};auto inside=[&](const Vertex& v){return side%2?coordinate(v)<=edge:coordinate(v)>=edge;};for(size_t i=0;i<count;++i){auto a=polygon[i],b=polygon[(i+1)%count];bool inA=inside(a),inB=inside(b);if(inA)scratch[output++]=a;if(inA!=inB){float t=(edge-coordinate(a))/(coordinate(b)-coordinate(a));scratch[output++]={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,tint};}}polygon=scratch;count=output;}
    if(count<3)return;if(texture.idx!=batchTexture_.idx||vertices_.size()+count>=4096){flush();batchTexture_=texture;}auto base=uint16_t(vertices_.size());vertices_.insert(vertices_.end(),polygon.begin(),polygon.begin()+count);for(uint16_t i=1;i+1<count;++i){indices_.push_back(base);indices_.push_back(base+i);indices_.push_back(base+i+1);}++stats_.quads;
}
Renderer::Texture Renderer::makeText(const std::string& s,int size) {
    ++stats_.textUploads;
    const auto ws=wide(s); HDC dc=CreateCompatibleDC(nullptr);
    HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto oldFont=SelectObject(dc,font); SIZE measure{}; GetTextExtentPoint32W(dc,ws.c_str(),int(ws.size()),&measure);
    const int w=std::clamp(int(measure.cx)+4,1,4096), h=std::max(int(measure.cy)+4,1);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr; auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap || !pixels) { SelectObject(dc,oldFont); DeleteObject(font); DeleteDC(dc); throw std::runtime_error("Cannot allocate font bitmap"); }
    auto oldBitmap=SelectObject(dc,bitmap); memset(pixels,0,size_t(w)*h*4); SetTextColor(dc,RGB(255,255,255)); SetBkColor(dc,RGB(0,0,0)); TextOutW(dc,0,0,ws.c_str(),int(ws.size())); GdiFlush();
    auto* rgba=static_cast<uint32_t*>(pixels); for(int i=0;i<w*h;++i) { const uint32_t a=rgba[i]&255; rgba[i]=0x00ffffff | (a<<24); }
    auto cleanup=[&](){SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);};
    try {auto texture=storeText(pixels,w,h);cleanup();return texture;} catch(...) {cleanup();throw;}
}
Renderer::Texture Renderer::storeText(const void* pixels,int w,int h) {
    constexpr int side=1024;
    const uint64_t flags=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    if(w+2>side || h+2>side) return {bgfx::createTexture2D(uint16_t(w),uint16_t(h),false,1,bgfx::TextureFormat::RGBA8,flags,bgfx::copy(pixels,uint32_t(w*h*4))),w,h,frame_};
    TextPage* chosen=nullptr;
    for(auto& page:textPages_) {
        if(page.x+w+1>side) {page.x=1;page.y+=page.rowHeight+2;page.rowHeight=0;}
        if(page.y+h+1<=side) {chosen=&page;break;}
    }
    if(!chosen) {
        if(textPages_.size()==4) {
            auto oldest=std::min_element(textPages_.begin(),textPages_.end(),[](const auto& a,const auto& b){return a.lastUsed<b.lastUsed;});
            flush();
            for(auto it=textCache_.begin();it!=textCache_.end();) {if(it->second.pooled && it->second.handle.idx==oldest->handle.idx) it=textCache_.erase(it);else ++it;}
            bgfx::destroy(oldest->handle);textPages_.erase(oldest);
        }
        std::vector<uint8_t> clear(side*side*4,0);
        // Supplying initial memory makes a D3D11 texture immutable. Atlas pages
        // must be created without memory, then populated through updateTexture2D.
        auto handle=bgfx::createTexture2D(side,side,false,1,bgfx::TextureFormat::RGBA8,flags);
        if(!bgfx::isValid(handle)) throw std::runtime_error("Cannot allocate text atlas");
        bgfx::updateTexture2D(handle,0,0,0,0,side,side,bgfx::copy(clear.data(),uint32_t(clear.size())));
        textPages_.push_back({handle});chosen=&textPages_.back();
    }
    bgfx::updateTexture2D(chosen->handle,0,0,uint16_t(chosen->x),uint16_t(chosen->y),uint16_t(w),uint16_t(h),bgfx::copy(pixels,uint32_t(w*h*4)));
    Texture result{chosen->handle,w,h,frame_,float(chosen->x)/side,float(chosen->y)/side,float(chosen->x+w)/side,float(chosen->y+h)/side,true};
    chosen->x+=w+2;chosen->rowHeight=std::max(chosen->rowHeight,h);chosen->lastUsed=frame_;return result;
}
Renderer::NumberAtlas Renderer::makeNumberAtlas(int size) {
    ++stats_.textUploads;HDC dc=CreateCompatibleDC(nullptr);
    HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto oldFont=SelectObject(dc,font);const std::wstring chars=L"0123456789-+";std::array<int,12> advances{};int cell=1,h=1;
    for(int i=0;i<12;++i) {SIZE measured{};GetTextExtentPoint32W(dc,&chars[i],1,&measured);advances[i]=int(measured.cx);cell=std::max(cell,int(measured.cx)+4);h=std::max(h,int(measured.cy)+4);}
    const int w=cell*12;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap || !pixels) {SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);throw std::runtime_error("Cannot create digit atlas");}
    auto oldBitmap=SelectObject(dc,bitmap);memset(pixels,0,size_t(w)*h*4);SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,RGB(0,0,0));
    for(int i=0;i<12;++i) TextOutW(dc,i*cell,0,&chars[i],1);GdiFlush();
    auto* rgba=static_cast<uint32_t*>(pixels);for(int i=0;i<w*h;++i) {auto a=rgba[i]&255;rgba[i]=0xffffff|(a<<24);}
    auto texture=bgfx::createTexture2D(uint16_t(w),uint16_t(h),false,1,bgfx::TextureFormat::RGBA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(pixels,uint32_t(w*h*4)));
    SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);
    return {{texture,w,h,frame_},cell,advances};
}
Renderer::NumberAtlas& Renderer::numberAtlas(int size) {
    auto it=numberAtlases_.find(size);if(it==numberAtlases_.end()) it=numberAtlases_.emplace(size,makeNumberAtlas(size)).first;return it->second;
}
void Renderer::text(float x,float y,const std::string& s,float size,uint32_t c,float maxWidth) {
    if(s.empty() || size<3 || maxWidth<=0) return;
    int px=std::clamp(int(size+0.5f),8,72);
    if(numeric(s)) {
        const auto& atlas=numberAtlas(px);float offset=0;
        for(char ch:s) {
            auto index=glyphIndex(ch);float w=std::min(float(atlas.advances[index]+4),maxWidth-offset);if(w<=0) break;
            const float u0=float(index*atlas.cellWidth)/atlas.texture.w,u1=(index*atlas.cellWidth+w)/atlas.texture.w;
            quad({x+offset,y,w,float(atlas.texture.h)},atlas.texture.handle,c,u1,u0);offset+=atlas.advances[index];
        }
        return;
    }
    auto key=std::to_string(px)+":"+s;
    auto it=textCache_.find(key); if(it==textCache_.end()) it=textCache_.emplace(key,makeText(s,px)).first;
    it->second.lastUsed=frame_;const auto& t=it->second; const float width=std::min(float(t.w),maxWidth);
    for(auto& page:textPages_) if(page.handle.idx==t.handle.idx) page.lastUsed=frame_;
    quad({x,y,width,float(t.h)},t.handle,c,t.u0+(t.u1-t.u0)*width/t.w,t.u0,t.v1,t.v0);
}
float Renderer::textWidth(const std::string& s,float size) {
    if(s.empty()) return 0;
    int px=std::clamp(int(size+0.5f),8,72);
    if(numeric(s)) {const auto& atlas=numberAtlas(px);float width=0;for(char ch:s) width+=atlas.advances[glyphIndex(ch)];return width;}
    auto key=std::to_string(px)+":"+s;
    auto it=textCache_.find(key); if(it==textCache_.end()) it=textCache_.emplace(key,makeText(s,px)).first;
    it->second.lastUsed=frame_;return float(it->second.w)-4;
}
}
