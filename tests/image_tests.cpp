#include "ui_compiler.hpp"
#include "image_import.hpp"
#include "runtime_document.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <chrono>

void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejects(F fn,const char* message) {bool rejected=false;try {fn();}catch(const std::exception&) {rejected=true;}check(rejected,message);}
bool near(float a,float b) {return std::abs(a-b)<.001f;}
void put(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {for(int i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(8*i));}
void rehash(std::vector<uint8_t>& bytes) {uint32_t hash=2166136261u;for(size_t i=32;i<bytes.size();++i) {hash^=bytes[i];hash*=16777619u;}put(bytes,28,hash);}
int main() {
    const auto root=std::filesystem::path(GM_SOURCE_DIR);
    const auto dir=std::filesystem::temp_directory_path()/("gm-image-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto rgba=gm::importImage(root/"tests/fixtures/images/rgba.png");
        check(rgba.width==2 && rgba.height==2 && rgba.rgba[3]==0 && rgba.rgba[7]==128 && rgba.rgba[11]==255,"PNG alpha lost");
        check(rgba.rgba[5]==255 && rgba.rgba[10]==255,"RGBA channel order changed");
        auto jpeg=gm::importImage(root/"tests/fixtures/images/rgb.jpg");
        check(jpeg.width==2 && jpeg.height==2 && jpeg.rgba[3]==255 && jpeg.rgba[15]==255,"JPEG import failed");
        rejects([&]{gm::importImage(root/"tests/fixtures/ui/images.ui.json");},"Unsupported source accepted");
        const auto v1=gm::loadUi(root/"tests/fixtures/counter-v1.gmui");
        check(v1.nodes.size()==9 && v1.images.empty(),"Version 1 compatibility lost");
        gm::RuntimeDocument legacy(v1);check(legacy.activate(5)==gm::ActionResult::Applied && legacy.nodes()[4].text=="1","Version 1 actions failed");
        auto p=gm::placeImage({10,20,100,100},200,100,gm::ImageFit::Contain);
        check(near(p.bounds.y,45) && near(p.bounds.h,50) && p.u0==0 && p.u1==1,"Contain placement incorrect");
        p=gm::placeImage({10,20,100,100},200,100,gm::ImageFit::Cover);
        check(near(p.u0,.25f) && near(p.u1,.75f) && p.bounds.h==100,"Cover UV incorrect");
        p=gm::placeImage({0,0,100,100},100,200,gm::ImageFit::Cover);
        check(near(p.v0,.25f) && near(p.v1,.75f),"Portrait cover UV incorrect");
        p=gm::placeImage({0,0,100,100},200,100,gm::ImageFit::Stretch);
        check(p.bounds.w==100 && p.bounds.h==100 && p.u0==0 && p.u1==1,"Stretch incorrect");
        const auto source=gm::readJson(root/"tests/fixtures/ui/images.ui.json");
        auto asset=gm::compileUi(source,root/"tests/fixtures/ui");
        check(asset.images.size()==2,"Repeated image was not deduplicated");
        bool clear=false,opaque=false,partial=false;
        for(size_t i=3;i<asset.images[0].rgba.size();i+=4) {auto a=asset.images[0].rgba[i];clear|=a==0;opaque|=a==255;partial|=a>0 && a<255;}
        check(clear && opaque && partial,"Alpha fixture lost transparent/partial/opaque pixels");
        auto bytes=gm::encodeUi(asset);check(bytes==gm::encodeUi(gm::decodeUi(bytes)),"Image binary roundtrip failed");
        check(std::string(bytes.begin(),bytes.end()).find("rgba.png")==std::string::npos,"Source path leaked into binary");
        auto bad=asset;bad.nodes[4].image=999;rejects([&]{gm::encodeUi(bad);},"Invalid image index accepted");
        bad=asset;bad.images[0].rgba.pop_back();rejects([&]{gm::encodeUi(bad);},"Invalid RGBA length accepted");
        const size_t table=32+144*asset.nodes.size()+32*asset.actions.size();
        auto corrupt=bytes;put(corrupt,table,0xffffffff);rehash(corrupt);rejects([&]{gm::decodeUi(corrupt);},"Oversized image dimensions accepted");
        corrupt=bytes;put(corrupt,table+8,4);rehash(corrupt);rejects([&]{gm::decodeUi(corrupt);},"Noncontiguous pixels accepted");
        corrupt=bytes;put(corrupt,table+12,4);rehash(corrupt);rejects([&]{gm::decodeUi(corrupt);},"Incorrect image byte length accepted");
        corrupt=bytes;corrupt.back()^=1;rejects([&]{gm::decodeUi(corrupt);},"Pixel corruption accepted");
        auto invalid=source;gm::findNode(invalid["root"],"alpha_image")->at("source")="missing.png";
        rejects([&]{gm::compileUi(invalid,root/"tests/fixtures/ui");},"Missing image accepted");
        invalid=source;gm::findNode(invalid["root"],"half")->at("action")["value"]=2;
        rejects([&]{gm::compileUi(invalid,root/"tests/fixtures/ui");},"Invalid opacity action accepted");
        gm::RuntimeDocument runtime(std::move(asset));const auto layout=runtime.layoutRevision(),revision=runtime.assetRevision();
        for(uint32_t i=0;i<runtime.nodes().size();++i) if(runtime.nodes()[i].kind==gm::UiKind::Button) check(runtime.activate(i)==gm::ActionResult::Applied,"Image action failed");
        check(runtime.layoutRevision()==layout && runtime.assetRevision()==revision,"Image paint action invalidated layout or textures");
        int affected=0;for(const auto& node:runtime.nodes()) if(node.kind==gm::UiKind::Image) {
            if(node.fit==gm::ImageFit::Stretch) ++affected;
            if((node.tint>>24)==128 && (node.tint&0xffffff)==0x8ad0ff) ++affected;
        }
        check(affected==2,"Fit, opacity or tint action result incorrect");
        std::filesystem::create_directories(dir);const auto documentPath=dir/"images.ui.json";gm::writeJsonAtomic(documentPath,source);gm::Document document(documentPath);
        check(document.execute({{"op","set"},{"id","alpha_image"},{"property","opacity"},{"value",.5}})["ok"],"CLI image edit failed");
        check(document.execute({{"op","undo"}})["ok"] && document.data()==source,"Image undo failed");
        check(!document.execute({{"op","set"},{"id","alpha_image"},{"property","fit"},{"value","invalid"}})["ok"].get<bool>(),"Invalid fit edit accepted");
        check(document.execute({{"op","create"},{"type","image"}})["ok"],"Create image failed");
        check(document.execute({{"op","undo"}})["ok"] && document.data()==source,"Create image undo failed");
        // Unicode source paths resolve against the UI location, independently of cwd.
        const auto unicode=dir/std::filesystem::path(u8"图片.png");std::filesystem::copy_file(root/"tests/fixtures/images/rgba.png",unicode);
        invalid=source;
        for(const auto* id:{"alpha_copy","rgb_image","alpha_image"}) (*gm::findNode(invalid["root"],id))["source"]=std::string(reinterpret_cast<const char*>(u8"图片.png"));
        check(gm::compileUi(invalid,dir).images.size()==1,"Unicode relative paths or deduplication failed");
        std::filesystem::remove_all(dir);
        std::cout<<"PASS: PNG alpha, JPEG, Unicode relative paths, image deduplication, v1 compatibility, v2 validation, fit geometry, paint-only actions, editor undo\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\nTest files: "<<dir<<'\n';return 1;}
}
