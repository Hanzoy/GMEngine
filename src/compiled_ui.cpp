#include "compiled_ui.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gm {
namespace {
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
bool geometry(float value) {return std::isfinite(value) && std::abs(value)<=16384;}
bool validUtf8(const std::string& text) {
    size_t i=0;
    while(i<text.size()) {
        uint32_t cp=uint8_t(text[i++]);int n=0;uint32_t minimum=0;
        if(cp<0x80) continue;
        if(cp>=0xc2 && cp<=0xdf) {n=1;cp&=0x1f;minimum=0x80;}
        else if(cp>=0xe0 && cp<=0xef) {n=2;cp&=0xf;minimum=0x800;}
        else if(cp>=0xf0 && cp<=0xf4) {n=3;cp&=7;minimum=0x10000;}
        else return false;
        while(n--) {if(i==text.size()) return false;auto b=uint8_t(text[i++]);if((b&0xc0)!=0x80) return false;cp=(cp<<6)|(b&0x3f);}
        if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
void checkText(const std::string& text) {require(text.size()<=2048 && validUtf8(text),"Invalid compiled UTF-8 text");}
uint32_t checksum(std::span<const uint8_t> bytes) {uint32_t h=2166136261u;for(auto b:bytes) {h^=b;h*=16777619u;}return h;}
void u32(std::vector<uint8_t>& bytes,uint32_t value) {for(int i=0;i<4;++i) bytes.push_back(uint8_t(value>>(8*i)));}
void u64(std::vector<uint8_t>& bytes,uint64_t value) {u32(bytes,uint32_t(value));u32(bytes,uint32_t(value>>32));}
void f32(std::vector<uint8_t>& bytes,float value) {u32(bytes,std::bit_cast<uint32_t>(value));}
struct Reader {
    std::span<const uint8_t> bytes;size_t offset=0;
    uint32_t u32() {require(offset+4<=bytes.size(),"Truncated UI resource");uint32_t v=0;for(int i=0;i<4;++i) v|=uint32_t(bytes[offset++])<<(i*8);return v;}
    int64_t i64() {uint64_t lo=u32(),hi=u32();return std::bit_cast<int64_t>(lo|(hi<<32));}
    float f32() {return std::bit_cast<float>(u32());}
};
}
bool parseCounter(const std::string& text,int64_t& value) {
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && value>=-1000000000 && value<=1000000000;
}
void validateCompiledUi(const CompiledUi& asset) {
    require(!asset.nodes.empty() && asset.nodes.size()<=65535 && asset.actions.size()<=65535,"Invalid compiled UI counts");
    require(asset.images.size()<=512,"Too many UI images");uint64_t imageBytes=0;
    for(const auto& image:asset.images) {
        require(image.width>0 && image.height>0 && image.width<=MaxImageDimension && image.height<=MaxImageDimension,"Invalid image dimensions");
        require(image.rgba.size()==uint64_t(image.width)*image.height*4,"Invalid RGBA image size");
        imageBytes+=image.rgba.size();require(imageBytes<=MaxImageBytes,"UI images exceed 64 MiB");
    }
    const auto& root=asset.nodes[0];
    require(root.kind==UiKind::Panel && root.parent==NoIndex && root.subtreeEnd==asset.nodes.size() && root.bounds.w>=320 && root.bounds.h>=240,"Invalid root canvas");
    std::vector<uint32_t> ancestors;
    for(uint32_t i=0;i<asset.nodes.size();++i) {
        const auto& node=asset.nodes[i];
        require(uint32_t(node.kind)<=uint32_t(UiKind::Image),"Unsupported compiled widget");
        require(uint32_t(node.fit)<=uint32_t(ImageFit::Stretch),"Invalid image fit");
        require(node.kind==UiKind::Image?node.image<asset.images.size():node.image==NoIndex,"Invalid image index");
        require(node.subtreeEnd>i && node.subtreeEnd<=asset.nodes.size(),"Invalid subtree range");
        while(!ancestors.empty() && i>=asset.nodes[ancestors.back()].subtreeEnd) ancestors.pop_back();
        if(i) require(!ancestors.empty() && node.parent==ancestors.back() && node.subtreeEnd<=asset.nodes[node.parent].subtreeEnd,"Invalid parent hierarchy");
        ancestors.push_back(i);require(ancestors.size()<=17,"Compiled UI is too deeply nested");
        require(geometry(node.bounds.x) && geometry(node.bounds.y) && geometry(node.bounds.w) && geometry(node.bounds.h) && node.bounds.w>=1 && node.bounds.h>=1,"Invalid compiled geometry");
        require(std::isfinite(node.fontSize) && node.fontSize>=10 && node.fontSize<=48,"Invalid font size");
        require((node.color>>24)==255 && (node.textColor>>24)==255,"Invalid compiled color");
        checkText(node.text);
        if(node.integerText) {int64_t value=0;require(parseCounter(node.text,value) && value==node.integerValue,"Invalid integer text binding");}
        if(node.action!=NoIndex) require(node.kind==UiKind::Button && node.action<asset.actions.size(),"Invalid button action index");
    }
    for(const auto& action:asset.actions) {
        require(uint32_t(action.op)<=uint32_t(UiOp::SetOpacity) && action.target<asset.nodes.size(),"Invalid compiled action");
        if(action.op>=UiOp::SetImageFit) require(asset.nodes[action.target].kind==UiKind::Image,"Image action requires an image target");
        checkText(action.text);
        switch(action.op) {
            case UiOp::AddInteger:require(asset.nodes[action.target].integerText && action.integer>=-1000000 && action.integer<=1000000,"Invalid counter action");break;
            case UiOp::SetText: {
                int64_t value=0;bool numeric=parseCounter(action.text,value);
                require(action.packed<=1 && bool(action.packed)==numeric && (!numeric || value==action.integer),"Invalid text action metadata");break;
            }
            case UiOp::SetColor:case UiOp::SetTextColor:case UiOp::SetTint:require((action.packed>>24)==255,"Invalid action color");break;
            case UiOp::SetImageFit:require(action.packed<=uint32_t(ImageFit::Stretch),"Invalid image fit action");break;
            case UiOp::SetOpacity:require(std::isfinite(action.number) && action.number>=0 && action.number<=1,"Invalid opacity action");break;
            case UiOp::SetVisible:require(action.packed<=1,"Invalid visibility action");break;
            case UiOp::SetX:case UiOp::SetY:require(geometry(action.number),"Invalid position action");break;
            case UiOp::SetWidth:require(geometry(action.number) && action.number>=(action.target==0?320:1),"Invalid width action");break;
            case UiOp::SetHeight:require(geometry(action.number) && action.number>=(action.target==0?240:1),"Invalid height action");break;
            case UiOp::SetFontSize:require(std::isfinite(action.number) && action.number>=10 && action.number<=48,"Invalid font action");break;
        }
    }
}
std::vector<uint8_t> encodeUi(const CompiledUi& asset) {
    validateCompiledUi(asset);std::vector<uint8_t> payload,pool;
    size_t pixelBytes=0;for(const auto& image:asset.images) pixelBytes+=image.rgba.size();
    payload.reserve(asset.nodes.size()*80+asset.actions.size()*32+asset.images.size()*16+pixelBytes);
    auto text=[&](const std::string& value) {u32(payload,uint32_t(pool.size()));u32(payload,uint32_t(value.size()));pool.insert(pool.end(),value.begin(),value.end());};
    for(const auto& node:asset.nodes) {
        u32(payload,uint32_t(node.kind));u32(payload,node.parent);u32(payload,node.subtreeEnd);u32(payload,node.action);
        f32(payload,node.bounds.x);f32(payload,node.bounds.y);f32(payload,node.bounds.w);f32(payload,node.bounds.h);
        u32(payload,node.color);u32(payload,node.textColor);f32(payload,node.fontSize);u32(payload,(node.visible?1:0)|(node.integerText?2:0));
        text(node.text);u64(payload,std::bit_cast<uint64_t>(node.integerValue));
        u32(payload,node.image);u32(payload,uint32_t(node.fit));u32(payload,node.tint);u32(payload,0);
    }
    for(const auto& action:asset.actions) {u32(payload,uint32_t(action.op));u32(payload,action.target);f32(payload,action.number);u32(payload,action.packed);u64(payload,std::bit_cast<uint64_t>(action.integer));text(action.text);}
    uint32_t pixelOffset=0;
    for(const auto& image:asset.images) {u32(payload,image.width);u32(payload,image.height);u32(payload,pixelOffset);u32(payload,uint32_t(image.rgba.size()));pixelOffset+=uint32_t(image.rgba.size());}
    payload.insert(payload.end(),pool.begin(),pool.end());
    for(const auto& image:asset.images) payload.insert(payload.end(),image.rgba.begin(),image.rgba.end());
    require(payload.size()+32<=128*1024*1024,"Compiled UI exceeds 128 MiB");
    std::vector<uint8_t> bytes={'G','M','U','I'};
    u32(bytes,2);u32(bytes,uint32_t(asset.nodes.size()));u32(bytes,uint32_t(asset.actions.size()));u32(bytes,uint32_t(pool.size()));
    u32(bytes,uint32_t(asset.images.size()));u32(bytes,uint32_t(pixelBytes));u32(bytes,checksum(payload));
    bytes.reserve(32+payload.size());
    bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
CompiledUi decodeUi(std::span<const uint8_t> bytes) {
    require(bytes.size()>=24 && bytes.size()<=128*1024*1024,"Invalid UI resource size");
    require(bytes[0]=='G' && bytes[1]=='M' && bytes[2]=='U' && bytes[3]=='I',"Expected compiled .gmui resource; compile JSON with gm_uic first");
    Reader r{bytes,4};const auto version=r.u32();require(version==1 || version==2,"Unsupported UI binary version");
    const auto nodes=r.u32(),actions=r.u32(),strings=r.u32();
    const auto images=version==2?r.u32():0,pixels=version==2?r.u32():0,hash=r.u32();
    const size_t header=version==2?32:24;
    require(nodes>0 && nodes<=65535 && actions<=65535,"Invalid resource counts");
    require(images<=512 && pixels<=MaxImageBytes,"Invalid image section counts");
    const uint64_t poolStart=header+(version==2?80ull:64ull)*nodes+32ull*actions+16ull*images;
    require(poolStart+strings+pixels==bytes.size(),"Invalid binary section sizes");
    require(checksum(bytes.subspan(header))==hash,"UI resource checksum mismatch");
    auto text=[&]() {uint32_t offset=r.u32(),length=r.u32();require(uint64_t(offset)+length<=strings && length<=2048,"Text offset is out of bounds");return std::string(reinterpret_cast<const char*>(bytes.data()+poolStart+offset),length);};
    CompiledUi asset;asset.nodes.reserve(nodes);asset.actions.reserve(actions);
    for(uint32_t i=0;i<nodes;++i) {
        UiNode node;node.kind=UiKind(r.u32());node.parent=r.u32();node.subtreeEnd=r.u32();node.action=r.u32();
        node.bounds={r.f32(),r.f32(),r.f32(),r.f32()};node.color=r.u32();node.textColor=r.u32();node.fontSize=r.f32();
        auto flags=r.u32();require(flags<=3,"Unknown node flags");node.visible=bool(flags&1);node.integerText=bool(flags&2);
        node.text=text();node.integerValue=r.i64();
        if(version==2) {node.image=r.u32();node.fit=ImageFit(r.u32());node.tint=r.u32();require(r.u32()==0,"Unknown image flags");}
        else require(node.kind<=UiKind::Button,"Invalid version 1 widget");
        asset.nodes.push_back(std::move(node));
    }
    for(uint32_t i=0;i<actions;++i) {UiAction action;action.op=UiOp(r.u32());action.target=r.u32();action.number=r.f32();action.packed=r.u32();action.integer=r.i64();action.text=text();asset.actions.push_back(std::move(action));}
    uint64_t expectedOffset=0;
    for(uint32_t i=0;i<images;++i) {
        UiImage image;image.width=r.u32();image.height=r.u32();const auto offset=r.u32(),length=r.u32();
        require(image.width>0 && image.height>0 && image.width<=MaxImageDimension && image.height<=MaxImageDimension,"Invalid image dimensions");
        require(offset==expectedOffset && uint64_t(offset)+length<=pixels && length==uint64_t(image.width)*image.height*4,"Invalid image pixel range");
        const auto start=bytes.begin()+size_t(poolStart)+strings+offset;image.rgba.assign(start,start+length);expectedOffset+=length;asset.images.push_back(std::move(image));
    }
    require(expectedOffset==pixels,"Unreferenced pixel data");
    validateCompiledUi(asset);return asset;
}
CompiledUi loadUi(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);require(size>=24 && size<=128*1024*1024,"Invalid UI file size");
    std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot open compiled UI");std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));require(bool(file),"Cannot read compiled UI");return decodeUi(bytes);
}
void saveUi(const std::filesystem::path& path,const CompiledUi& asset) {
    const auto bytes=encodeUi(asset);if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+="."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";
    try {
        std::ofstream out(temp,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.flush();require(bool(out),"Cannot write UI resource");out.close();
#ifdef _WIN32
        require(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot replace compiled UI");
#else
        std::filesystem::rename(temp,path);
#endif
    } catch(...) {std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
}
