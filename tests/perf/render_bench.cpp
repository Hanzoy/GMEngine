#include "runtime_view.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
using Clock=std::chrono::steady_clock;
template<class T> void discardPixels(T& doc) {if constexpr(requires {doc.releaseImagePixels();}) doc.releaseImagePixels();}
int main(int argc,char** argv) {
    try {
        if(argc!=2) return 2;std::vector<double> loads;
        for(int r=0;r<7;++r) {auto start=Clock::now();gm::RuntimeDocument doc{std::filesystem::path(argv[1])};loads.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());}
        std::sort(loads.begin(),loads.end());
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GMRenderBenchmark";RegisterClassW(&cls);
        auto window=CreateWindowW(cls.lpszClassName,L"GM render benchmark",WS_OVERLAPPEDWINDOW,0,0,960,600,nullptr,nullptr,cls.hInstance,nullptr);
        {gm::Renderer renderer(window,960,600);gm::RuntimeView view;gm::RuntimeDocument doc{std::filesystem::path(argv[1])};
        renderer.begin(960,600);view.render(renderer,doc,960,600);renderer.end();discardPixels(doc);
        std::cout<<"case,load_ms,frontend_us,draw_calls,quads,text_uploads,image_uploads,cpu_image_bytes\n"<<std::fixed<<std::setprecision(3);
        auto run=[&](const char* name,gm::RuntimeDocument& document) {
            std::vector<double> samples;uint64_t upload=0;gm::Renderer::Stats stats;
            for(int i=0;i<80;++i) {renderer.begin(960,600);auto start=Clock::now();view.render(renderer,document,960,600);auto end=Clock::now();renderer.end();discardPixels(document);stats=renderer.stats();if(i>=20) {samples.push_back(std::chrono::duration<double,std::micro>(end-start).count());upload+=stats.textUploads+stats.imageUploads;}}
            std::sort(samples.begin(),samples.end());uint64_t bytes=0;for(const auto& image:document.images()) bytes+=image.rgba.capacity();
            std::cout<<name<<','<<loads[3]<<','<<samples[samples.size()/2]<<','<<stats.drawCalls<<','<<stats.quads<<','<<stats.textUploads<<','<<stats.imageUploads<<','<<bytes<<'\n';
            if(upload) throw std::runtime_error("Warm frames allocated textures");
        };
        run("images",doc);
        gm::CompiledUi labels;gm::UiNode root;root.bounds={0,0,960,600};root.subtreeEnd=301;labels.nodes.push_back(root);
        for(uint32_t i=0;i<300;++i) {gm::UiNode label;label.kind=gm::UiKind::Label;label.parent=0;label.subtreeEnd=i+2;label.bounds={float((i%12)*80),float((i/12)*24),80,24};label.text="Item "+std::to_string(i);labels.nodes.push_back(label);}
        gm::RuntimeDocument grid(std::move(labels));run("300_labels",grid);
        }DestroyWindow(window);return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
