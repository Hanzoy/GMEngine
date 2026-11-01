#include "render_assets.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool condition) { if (!condition) throw std::runtime_error("Render budget contract failed"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } check(rejected); }
void rehash(std::vector<uint8_t>& b) {
    uint32_t h=2166136261u; for(size_t i=12;i<b.size();++i){h^=b[i];h*=16777619u;}
    for(int i=0;i<4;++i)b[8+i]=uint8_t(h>>(8*i));
}
}
int main() {
    try {
        check(gm::RenderResourceBudget==1024*1024);
        check(gm::SceneBinaryBudget==65ull*1024*1024);
        check(gm::renderPayloadFits(gm::RenderResourceBudget,0));
        check(!gm::renderPayloadFits(gm::RenderResourceBudget,1));
        check(!gm::renderPayloadFits(UINT64_MAX,1));
        check(!gm::renderPayloadFits(1,UINT64_MAX));
        gm::RenderAssets bank;
        gm::CookedTexture t{512,256,1,true,true,true,{{gm::TextureFormat::Rgba8,std::vector<uint8_t>(512*256*4,127)}}};
        bank.textures.push_back(t);
        auto bytes=gm::encodeRenderAssets(bank);
        check(gm::decodeRenderAssets(bytes).textures[0].variants[0].bytes==t.variants[0].bytes);
        // Valid texture payload with a truthful checksum, but a truncated section.
        bytes.pop_back(); rehash(bytes);
        rejects([&]{gm::decodeRenderAssets(bytes);});
        // Encoding metadata must fit too, even when the pixel payload fits exactly.
        bank.textures.push_back(t);
        gm::validateRenderAssets(bank);
        rejects([&]{gm::encodeRenderAssets(bank);});
        bank.textures.push_back(t);
        rejects([&]{gm::validateRenderAssets(bank);});
        std::vector<uint8_t> tooLarge(gm::RenderResourceBudget+13);
        rejects([&]{gm::decodeRenderAssets(tooLarge);});
        std::cout<<"Configured render budget: roundtrip, boundaries, overflow and truncation passed\n";
        return 0;
    } catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 1;}
}
