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
                if(renderer.stats().imageUploads!=2 || renderer.imageTextureCount()!=2) throw std::runtime_error("Image reload leaked GPU textures");
            }
            for(uint32_t i=0;i<images.nodes().size();++i) if(images.nodes()[i].kind==gm::UiKind::Button) images.activate(i);
            std::vector<uint32_t> tints;for(const auto& node:images.nodes()) tints.push_back(node.tint);
            images.restoreImagePixels();
            if(images.images()[0].rgba.empty()) throw std::runtime_error("Restoring pixels failed");
            for(size_t i=0;i<tints.size();++i) if(images.nodes()[i].tint!=tints[i]) throw std::runtime_error("Restoring pixels lost runtime state");
            images.releaseImagePixels();
            renderer.begin(960,600);renderer.rect({0,0,960,600},0xff000000);
            for(int i=0;i<300;++i) {renderer.clip({float(i%12)*80,float(i/12)*24,80,24});renderer.text(float(i%12)*80,float(i/12)*24,"Item "+std::to_string(i),14,0xffffffff,80);}
            renderer.end();
            if(renderer.stats().drawCalls!=2) throw std::runtime_error("300 clipped labels did not share a text atlas batch");
            std::cout<<"PASS: 2 unique image uploads, no uploads/layout rebuilds for 7 image actions, no texture leaks across 3 reloads\n";
            std::cout<<"PASS: solid batching, "<<initialDraws<<" counter draw calls, zero text texture uploads for 20 increments, one layout build\n";
        }
        DestroyWindow(window);return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
