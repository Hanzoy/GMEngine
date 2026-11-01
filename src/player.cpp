#include "runtime_view.hpp"
#include "scene_view.hpp"
#include "scene_catalog.hpp"
#include "windows_game_input.hpp"
#include <chrono>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <imm.h>
#include <memory>
#include <stdexcept>

namespace {
class Player {
public:
    Player(HWND window,const std::filesystem::path& path,std::unique_ptr<gm::CompiledScene> compiled):window_(window),path_(path) {
        RECT area;GetClientRect(window_,&area);renderer_=std::make_unique<gm::Renderer>(window_,area.right,area.bottom);
        if(compiled) {scene_=std::make_unique<gm::SceneRuntime>(std::move(*compiled),path.parent_path());sceneView_=std::make_unique<gm::SceneView>(*renderer_);ImmAssociateContextEx(window_,nullptr,0);}else document_=std::make_unique<gm::RuntimeDocument>(path);
        auto catalog=path.parent_path()/"scenes.gmcatalog";if(scene_&&std::filesystem::exists(catalog))loader_=std::make_unique<gm::SceneLoader>(catalog);
    }
    ~Player(){sceneView_.reset();view_.clear(*renderer_);}
    bool needsFrame() const {return scene_||(redraw_&&!IsIconic(window_));}
    DWORD waitTime() const {return scene_?(GetForegroundWindow()==window_&&!IsIconic(window_)?16:100):INFINITE;}
    void requestFrame() {redraw_=true;}
    void inputKey(WPARAM key, bool repeat){gameInput_.key(uint32_t(key),repeat);}
    bool inputTouch(WPARAM p,bool down){return gameInput_.pointer(window_,p,down);}
    void inputButton(uint32_t bit,bool down){gameInput_.button(bit,down);if(down)SetCapture(window_);else if(bit!=1&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&!(GetAsyncKeyState(VK_MBUTTON)&0x8000)&&!(GetAsyncKeyState(VK_RBUTTON)&0x8000))ReleaseCapture();}
    void inputWheel(float amount){gameInput_.wheel(amount);}
    void loseFocus(){gameInput_.clear();}
    void key(WPARAM key){if(!scene_)return;if(key=='D'||key==VK_RIGHT)pending_.x=1;if(key=='A'||key==VK_LEFT)pending_.x=-1;if(key=='S'||key==VK_DOWN)pending_.y=1;if(key=='W'||key==VK_UP)pending_.y=-1;}
    void frame() {
        if(scene_)try{
            auto request=scene_->takeSceneRequest();
            if(!request.empty()){if(!loader_)throw std::runtime_error("No scene catalog; build/package the project first");loader_->request(request);}
            if(loader_)if(auto ready=loader_->poll()){
                auto next=std::make_unique<gm::SceneRuntime>(std::move(*ready),path_.parent_path());
                sceneView_->clear();scene_=std::move(next);lastLoadError_.clear();
                pressed_=gm::NoIndex;ReleaseCapture();lastTick_=std::chrono::steady_clock::now();
            }
        }catch(const std::exception& error){
            if(lastLoadError_!=error.what()){lastLoadError_=error.what();MessageBoxW(window_,gm::wide(lastLoadError_).c_str(),L"Scene load failed",MB_OK|MB_ICONERROR);}
            lastTick_=std::chrono::steady_clock::now();
        }
        RECT area;GetClientRect(window_,&area);
        if(scene_){
            auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-lastTick_).count();lastTick_=now;
            gm::SceneInput input;bool focused=GetForegroundWindow()==window_&&!IsIconic(window_);
            if(focused){auto held=[](int k){return (GetAsyncKeyState(k)&0x8000)!=0;};input.x=float(held('D')||held(VK_RIGHT))-float(held('A')||held(VK_LEFT));input.y=float(held('S')||held(VK_DOWN))-float(held('W')||held(VK_UP));if(input.x==0)input.x=pending_.x;if(input.y==0)input.y=pending_.y;}
            pending_={};input.pointer=gameInput_.sample(window_,{0,0,float(area.right),float(area.bottom)},scene_->asset().width,scene_->asset().height,focused);
            scene_->advance(dt,input);
            if(area.right>0&&area.bottom>0&&!IsIconic(window_)){renderer_->begin(area.right,area.bottom);sceneView_->render(*scene_,{0,0,float(area.right),float(area.bottom)});renderer_->end();}
            redraw_=false;return;
        }
        if(area.right<1||area.bottom<1||IsIconic(window_))return;
        renderer_->begin(area.right,area.bottom);view_.render(*renderer_,*document_,float(area.right),float(area.bottom));
        const auto hovered=view_.layout.hit(mouseX_,mouseY_);
        if(hovered!=gm::NoIndex) renderer_->border(view_.layout.clips()[hovered],gm::color(pressed_==hovered?"#FFFFFF":"#AFC9FF"),2);
        renderer_->end();redraw_=false;
    }
    void down(float x,float y) {
        if(scene_){pressed_=sceneView_->hitUi(*scene_,x,y);if(pressed_!=gm::NoIndex)SetCapture(window_);return;}
        updateLayout();mouseX_=x;mouseY_=y;pressed_=view_.layout.hit(x,y);requestFrame();
        if(pressed_!=gm::NoIndex) SetCapture(window_);
    }
    void up(float x,float y) {
        if(scene_){pressed_=gm::NoIndex;ReleaseCapture();return;}
        const auto pressed=pressed_;pressed_=gm::NoIndex;ReleaseCapture();requestFrame();updateLayout();
        if(pressed==gm::NoIndex || view_.layout.hit(x,y)!=pressed) return;
        const auto result=document_->activate(pressed);
        if(result==gm::ActionResult::OutOfRange || result==gm::ActionResult::InvalidCounter) throw std::runtime_error("Counter action rejected: invalid value or range");
        updateLayout();
    }
    void move(float x,float y) {
        if(scene_){SetCursor(LoadCursor(nullptr,sceneView_->hitUi(*scene_,x,y)==gm::NoIndex?IDC_ARROW:IDC_HAND));return;}
        updateLayout();auto previous=view_.layout.hit(mouseX_,mouseY_);mouseX_=x;mouseY_=y;auto current=view_.layout.hit(x,y);
        if(previous!=current) requestFrame();SetCursor(LoadCursor(nullptr,current==gm::NoIndex?IDC_ARROW:IDC_HAND));
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_LEAVE,window_,0};TrackMouseEvent(&tracking);
    }
    void leave() {mouseX_=mouseY_=-1;requestFrame();}
    void cancel() {pressed_=gm::NoIndex;gameInput_.cancelButtons();requestFrame();}
    void reload() {if(scene_){auto next=std::make_unique<gm::SceneRuntime>(gm::loadScene(path_),path_.parent_path());sceneView_->clear();scene_=std::move(next);lastTick_=std::chrono::steady_clock::now();}else document_->reload();cancel();ReleaseCapture();requestFrame();}
private:
    void updateLayout() {RECT area;GetClientRect(window_,&area);if(area.right>0 && area.bottom>0) view_.layout.update(*document_,float(area.right),float(area.bottom));}
    HWND window_;std::filesystem::path path_;std::unique_ptr<gm::RuntimeDocument> document_;gm::RuntimeView view_;
    std::unique_ptr<gm::Renderer> renderer_;
    std::unique_ptr<gm::SceneRuntime> scene_;std::unique_ptr<gm::SceneView> sceneView_;
    std::unique_ptr<gm::SceneLoader> loader_;
    std::string lastLoadError_;
    std::chrono::steady_clock::time_point lastTick_=std::chrono::steady_clock::now();
    gm::SceneInput pending_{};
    gm::WindowsGameInput gameInput_;
    uint32_t pressed_=gm::NoIndex;float mouseX_=-1,mouseY_=-1;bool redraw_=true;
};
Player* player=nullptr;
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    try {
        if(message==WM_GETMINMAXINFO) {reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={480,340};return 0;}
        if(message==WM_PAINT) {PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);if(player) player->requestFrame();return 0;}
        if(player) switch(message) {
            case WM_SIZE:player->requestFrame();return 0;
            case WM_LBUTTONDOWN:player->inputButton(1,true);SetFocus(window);player->down(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_LBUTTONUP:player->inputButton(1,false);player->up(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_MOUSEMOVE:player->move(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_MOUSELEAVE:player->leave();return 0;
            case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:player->inputButton(message==WM_RBUTTONDOWN?2:4,true);return 0;
            case WM_RBUTTONUP:case WM_MBUTTONUP:player->inputButton(message==WM_RBUTTONUP?2:4,false);return 0;
            case WM_MOUSEWHEEL:player->inputWheel(float(GET_WHEEL_DELTA_WPARAM(wp))/120.f);return 0;
            case WM_KILLFOCUS:player->loseFocus();return 0;
            case WM_CAPTURECHANGED:player->cancel();return 0;
            case WM_POINTERDOWN:case WM_POINTERUPDATE:case WM_POINTERUP:if(player->inputTouch(wp,message!=WM_POINTERUP))return 0;break;
            case WM_KEYDOWN:player->inputKey(wp,(lp & (1ll<<30))!=0);if(wp==VK_F5) {player->reload();return 0;}player->key(wp);break;
        }
        if(message==WM_ERASEBKGND) return 1;
        if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    } catch(const std::exception& error) {MessageBoxW(window,gm::wide(error.what()).c_str(),L"GM Player",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    bool checkOnly=false;
    try {
        SetProcessDPIAware();wchar_t module[32768];auto length=GetModuleFileNameW(nullptr,module,32768);
        if(!length || length==32768) throw std::runtime_error("Cannot resolve player directory");
        const auto playerDirectory=std::filesystem::path(module).parent_path();std::filesystem::path gameModule;
        auto project=playerDirectory/"main.gmui";
        const auto legacyMain=playerDirectory/"assets/ui/main.gmui";
        if(!std::filesystem::exists(project)&&std::filesystem::exists(legacyMain))project=legacyMain;
        if(std::filesystem::exists(playerDirectory/"game.gmscene"))project=playerDirectory/"game.gmscene";
        if(std::filesystem::exists(playerDirectory/"game.dll"))gameModule=playerDirectory/"game.dll";
        int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);if(!args) throw std::runtime_error("Cannot read command line");
        bool valid=true;
        for(int i=1;i<argc;++i) {
            if(std::wstring(args[i])==L"--project" && i+1<argc) project=args[++i];
            else if(std::wstring(args[i])==L"--module" && i+1<argc) gameModule=args[++i];
            else if(std::wstring(args[i])==L"--check") checkOnly=true;
            else {valid=false;break;}
        }
        LocalFree(args);if(!valid) throw std::runtime_error("Usage: gm_player.exe [--project FILE.gmui|FILE.gmscene] [--module GAME.dll] [--check]");
        if(project.extension()==".gmscene"&&!gameModule.empty()){auto registry=std::make_shared<gm::GameRegistry>();registry->load(gameModule);gm::useGameRegistry(registry);}
        if(checkOnly) {if(project.extension()==".gmscene"){gm::SceneRuntime verify(gm::loadScene(project));verify.tick(1.f/60,{1,0});}else {gm::RuntimeDocument verify(project);}return 0;}
        WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=instance;cls.lpszClassName=L"GMEnginePlayer";
        cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
        if(!RegisterClassW(&cls)) throw std::runtime_error("Cannot register player window");
        std::unique_ptr<gm::CompiledScene> initialScene;
        RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int desiredWidth=1280,desiredHeight=720;if(project.extension()==".gmscene"){initialScene=std::make_unique<gm::CompiledScene>(gm::loadScene(project));const auto& asset=*initialScene;float fit=std::min(1.f,std::min(float(work.right-work.left-64)/asset.width,float(work.bottom-work.top-80)/asset.height));desiredWidth=int(asset.width*fit);desiredHeight=int(asset.height*fit);}
        RECT bounds{0,0,desiredWidth,desiredHeight};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
        const auto title=project.filename().wstring()+L" — GM Engine Player";
        HWND window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
        if(!window) throw std::runtime_error("Cannot create player window");
        Player instancePlayer(window,project,std::move(initialScene));player=&instancePlayer;ShowWindow(window,SW_SHOW);UpdateWindow(window);
        MSG msg{};bool running=true;
        while(running) {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                if(msg.message==WM_QUIT) {running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);
            }
            if(!running) break;
            if(instancePlayer.needsFrame()) {instancePlayer.frame();if(instancePlayer.waitTime()!=INFINITE)MsgWaitForMultipleObjectsEx(0,nullptr,instancePlayer.waitTime(),QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
            else MsgWaitForMultipleObjectsEx(0,nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        player=nullptr;return 0;
    } catch(const std::exception& error) {
        player=nullptr;if(!checkOnly) MessageBoxW(nullptr,gm::wide(error.what()).c_str(),L"GM Player",MB_OK|MB_ICONERROR);return 1;
    }
}
