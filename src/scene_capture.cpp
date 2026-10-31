// Deterministic native scene rendering for game QA. Renders its own hidden surface;
// it never captures the desktop, injects OS input, or interacts with other apps.
#include "project.hpp"
#include "scene_view.hpp"
#include "render_diagnostics.hpp"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <lodepng.h>
namespace {
struct Capture final : bgfx::CallbackI {
    std::filesystem::path output;std::atomic<int> result=0;
    void fatal(const char*,uint16_t,bgfx::Fatal::Enum,const char* message)override{std::cerr<<message<<'\n';std::abort();}
    void traceVargs(const char*,uint16_t,const char*,va_list)override{}
    void profilerBegin(const char*,uint32_t,const char*,uint16_t)override{}
    void profilerBeginLiteral(const char*,uint32_t,const char*,uint16_t)override{}
    void profilerEnd()override{}
    uint32_t cacheReadSize(uint64_t)override{return 0;}
    bool cacheRead(uint64_t,void*,uint32_t)override{return false;}
    void cacheWrite(uint64_t,const void*,uint32_t)override{}
    void screenShot(const char*,uint32_t w,uint32_t h,uint32_t pitch,bgfx::TextureFormat::Enum format,const void* data,uint32_t size,bool flip)override{
        try {
            if(uint64_t(pitch)*h>size||pitch<w*4||w>8192||h>8192)throw std::runtime_error("Invalid capture buffer");
            if(format!=bgfx::TextureFormat::BGRA8&&format!=bgfx::TextureFormat::RGBA8)throw std::runtime_error("Unsupported capture format");
            std::vector<uint8_t> rgba(size_t(w)*h*4);auto* src=static_cast<const uint8_t*>(data);
            for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){auto a=(flip?h-1-y:y)*pitch+x*4,b=(y*w+x)*4;rgba[b]=src[a+(format==bgfx::TextureFormat::BGRA8?2:0)];rgba[b+1]=src[a+1];rgba[b+2]=src[a+(format==bgfx::TextureFormat::BGRA8?0:2)];rgba[b+3]=255;}
            std::vector<uint8_t> png;auto code=lodepng::encode(png,rgba,w,h);std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));file.flush();result.store(code==0&&file?1:-1);
        }catch(...){result.store(-1);}
    }
    void captureBegin(uint32_t,uint32_t,uint32_t,bgfx::TextureFormat::Enum,bool)override{}
    void captureEnd()override{}void captureFrame(const void*,uint32_t)override{}
};
}
int main(int argc,char** argv){try{
    if(argc<4||argc>7){std::cerr<<"gm_scene_capture SCENE.gmscene MODULE.dll OUTPUT.png [INPUT.json] [WIDTH HEIGHT]\n";return 2;}
    auto registry=std::make_shared<gm::GameRegistry>();registry->load(argv[2]);gm::useGameRegistry(registry);gm::SceneRuntime scene(gm::loadScene(argv[1]),std::filesystem::path(argv[1]).parent_path());
    scene.tick(1.f/60,{});
    if(argc>=5&&std::string(argv[4])!="-")for(auto& request:gm::readJson(argv[4])) {auto response=gm::runtimeCommand(scene,request);if(!response.value("ok",false))throw std::runtime_error(response.dump());}
    int width=argc==7?std::stoi(argv[5]):int(scene.asset().width),height=argc==7?std::stoi(argv[6]):int(scene.asset().height);if(width<64||height<64||width>4096||height>4096)throw std::runtime_error("Capture dimensions must be 64..4096");
    Capture callback;callback.output=std::filesystem::path(argv[3]);std::filesystem::create_directories(std::filesystem::absolute(callback.output).parent_path());
    auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=instance;cls.lpszClassName=L"GMSceneCapture";RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"Scene render QA",WS_OVERLAPPEDWINDOW,0,0,width,height,nullptr,nullptr,instance,nullptr);if(!window)throw std::runtime_error("Cannot create render surface");
    {
        gm::Renderer renderer(window,uint32_t(width),uint32_t(height),&callback);gm::SceneView view(renderer);
        for(int i=0;i<5;++i){renderer.begin(width,height);view.render(scene,{0,0,float(width),float(height)});renderer.end();}
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE,"capture");
        for(int i=0;i<120&&!callback.result.load();++i){renderer.begin(width,height);view.render(scene,{0,0,float(width),float(height)});renderer.end();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        gm::writeJsonAtomic(std::filesystem::path(argv[3]).concat(".stats.json"),gm::renderDiagnostics(renderer.stats()));
        std::cout<<"canvas="<<scene.canvas().size()<<" quads="<<renderer.stats().quads<<" draws="<<renderer.stats().drawCalls<<" textUploads="<<renderer.stats().textUploads<<" textEntries="<<renderer.stats().textCacheEntries<<" textBytes="<<renderer.stats().textTextureBytes<<" simulationMs="<<scene.stats().simulationMs<<'\n';
    }
    DestroyWindow(window);if(callback.result.load()!=1)throw std::runtime_error("GPU screenshot failed/timed out");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
