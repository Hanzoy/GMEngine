#include "runtime_view.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=3) return 2;auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=instance;cls.lpszClassName=L"GMRenderTest";RegisterClassW(&cls);
        HWND window=CreateWindowW(cls.lpszClassName,L"GM renderer test",WS_OVERLAPPEDWINDOW,0,0,960,600,nullptr,nullptr,instance,nullptr);
        if(!window) throw std::runtime_error("Test window unavailable");
        {
            gm::Renderer renderer(window,960,600);gm::RuntimeDocument document{std::filesystem::path(argv[1])};gm::RuntimeView view;
            renderer.begin(960,600);renderer.rect({0,0,100,100},0xffffffff);renderer.rect({100,0,100,100},0xff0000ff);renderer.end();
            if(renderer.stats().quads!=2 || renderer.stats().drawCalls!=1) throw std::runtime_error("Adjacent solid quads did not batch");
            renderer.begin(960,600);view.render(renderer,document,960,600);renderer.end();
            const auto initialDraws=renderer.stats().drawCalls;
            std::cout<<"Counter draws: "<<initialDraws<<'\n';
            if(initialDraws>9) throw std::runtime_error("Counter batching regressed");
            for(int i=0;i<20;++i) {
                document.activate(5);renderer.begin(960,600);view.render(renderer,document,960,600);renderer.end();
                if(renderer.stats().textUploads!=0) throw std::runtime_error("Numeric change uploaded a new texture");
            }
            if(view.layout.rebuilds()!=1) throw std::runtime_error("Counter updates rebuilt geometry");
            gm::RuntimeDocument images{std::filesystem::path(argv[2])};
            renderer.begin(960,600);view.render(renderer,images,960,600);renderer.end();
            if(renderer.stats().imageUploads!=2 || renderer.imageTextureCount()!=2) throw std::runtime_error("Shared image uploaded more than once");
            for(const auto& image:images.images()) if(image.rgba.capacity()!=0) throw std::runtime_error("Uploaded pixels retained CPU storage");
            const auto builds=view.layout.rebuilds();
            for(uint32_t i=0;i<images.nodes().size();++i) if(images.nodes()[i].kind==gm::UiKind::Button) {
                images.activate(i);renderer.begin(960,600);view.render(renderer,images,960,600);renderer.end();
                if(renderer.stats().imageUploads || view.layout.rebuilds()!=builds) throw std::runtime_error("Image property change reuploaded texture or rebuilt layout");
            }
            for(int i=0;i<3;++i) {
                images.reload();renderer.begin(960,600);view.render(renderer,images,960,600);renderer.end();
                if(renderer.stats().imageUploads!=0 || renderer.imageTextureCount()!=2) throw std::runtime_error("Unchanged reload reuploaded or leaked GPU textures");
            }
            gm::RuntimeDocument secondImages{std::filesystem::path(argv[2])};gm::RuntimeView secondView;
            renderer.begin(960,600);secondView.render(renderer,secondImages,960,600);renderer.end();
            if(renderer.stats().imageUploads||renderer.imageTextureCount()!=2)throw std::runtime_error("Images not shared across views");
            secondView.clear(renderer);
            if(renderer.imageTextureCount()!=2)throw std::runtime_error("Releasing one view destroyed shared images");
            for(int frame=0;frame<6;++frame){renderer.begin(960,600);view.render(renderer,images,960,600);renderer.end();}
            if(!renderer.stats().cachedDraws||renderer.stats().vertexUploadBytes||renderer.stats().geometryBuilds)throw std::runtime_error("Static UI rebuilt or uploaded vertices");
            renderer.begin(800,600);view.render(renderer,images,800,600);renderer.end();
            if(!renderer.stats().vertexUploadBytes)throw std::runtime_error("Resize did not invalidate cached geometry");
            for(uint32_t i=0;i<images.nodes().size();++i) if(images.nodes()[i].kind==gm::UiKind::Button) images.activate(i);
            std::vector<uint32_t> tints;for(const auto& node:images.nodes()) tints.push_back(node.tint);
            images.restoreImagePixels();
            if(images.images()[0].rgba.empty()) throw std::runtime_error("Restoring pixels failed");
            for(size_t i=0;i<tints.size();++i) if(images.nodes()[i].tint!=tints[i]) throw std::runtime_error("Restoring pixels lost runtime state");
            images.releaseImagePixels();
            renderer.begin(960,600);renderer.rect({0,0,960,600},0xff000000);
            for(int i=0;i<300;++i) {renderer.clip({float(i%12)*80,float(i/12)*24,80,24});renderer.text(float(i%12)*80,float(i/12)*24,"Item "+std::to_string(i),14,0xffffffff,80);}
            renderer.end();
            if(renderer.stats().drawCalls>3) throw std::runtime_error("300 clipped labels did not share a text atlas batch");
            renderer.begin(960,600);renderer.text(0,0,"供电 123.45% / 678.90 MW",14,0xffffffff);renderer.end();
            for(int frame=0;frame<300;++frame){
                renderer.begin(960,600);
                for(int row=0;row<30;++row){renderer.rect({0,float(row*18),600,18},0xff223344);renderer.text(0,float(row*18),"供电 "+std::to_string(frame*30+row)+".25% / 670.20 MW",14,0xffffffff);}
                renderer.end();
                if(renderer.stats().textUploads!=0)throw std::runtime_error("Changing mixed numeric labels uploaded new text");
                if(renderer.stats().drawCalls>4)throw std::runtime_error("Interleaved text and solids did not batch in order");
                if(renderer.stats().textTextureBytes>42ull*1024*1024||renderer.stats().textCacheEntries>2048)throw std::runtime_error("Text cache exceeded budget");
            }
            uint32_t evictions=0;
            for(int frame=0;frame<120;++frame){
                renderer.begin(960,600);
                for(int row=0;row<10;++row){int value=frame*10+row;std::string label;for(int n=0;n<4;++n){label+=char('A'+value%26);value/=26;}label+=std::string(frame%2?100:20,'W');renderer.text(0,float(row*40),label,24,0xffffffff);renderer.text(600,float(row*40),std::to_string(frame),24,0xffffffff);}
                renderer.end();evictions+=renderer.stats().textEvictions;
                if(renderer.stats().textTextureBytes>42ull*1024*1024||renderer.stats().textCacheEntries>2048||renderer.stats().textAtlasPages>4)throw std::runtime_error("Unique text churn exceeded cache budget");
            }
            if(!evictions)throw std::runtime_error("Unique text churn did not exercise eviction");
            gm::CompiledUi listAsset;gm::UiNode root;root.bounds={0,0,960,600};root.subtreeEnd=2;listAsset.nodes.push_back(root);
            gm::UiNode list;list.kind=gm::UiKind::List;list.id="rows";list.parent=0;list.subtreeEnd=2;list.bounds={20,20,400,280};list.rowHeight=28;listAsset.nodes.push_back(list);
            gm::RuntimeDocument listDocument(listAsset);std::vector<std::string> rows(10000,"Visible row");std::vector<const char*> pointers;for(auto &row:rows)pointers.push_back(row.c_str());listDocument.setList(1,pointers.data(),uint32_t(pointers.size()));
            gm::RuntimeView listView;renderer.begin(960,600);listView.render(renderer,listDocument,960,600);renderer.end();
            if(renderer.stats().quads>32)throw std::runtime_error("List did not virtualize 10,000 rows");
            listDocument.scroll(1,280000);renderer.begin(960,600);listView.render(renderer,listDocument,960,600);renderer.end();
            if(renderer.stats().quads>32)throw std::runtime_error("Scrolled list did not virtualize rows");listView.clear(renderer);
            view.clear(renderer);renderer.trimImages(true);renderer.begin(960,600);renderer.end();
            if(renderer.stats().imageTextureBytes||renderer.imageTextureCount())throw std::runtime_error("Unused image pool did not release GPU memory");
            std::cout<<"PASS: shared textures, unchanged reload, stable UI buffers, resize invalidation, pool reclamation\n";
            std::cout<<"PASS: dynamic mixed labels reuse textures; unique text churn evicts within budget\n";
            std::cout<<"PASS: 2 unique image uploads, no uploads/layout rebuilds for 7 image actions, no texture leaks across 3 reloads\n";
            std::cout<<"PASS: solid batching, "<<initialDraws<<" counter draw calls, zero text texture uploads for 20 increments, one layout build\n";
        }
        DestroyWindow(window);return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
