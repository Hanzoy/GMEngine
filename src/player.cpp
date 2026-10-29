#include "runtime_view.hpp"
#include "scene_view.hpp"
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
    Player(HWND window,const std::filesystem::path& path):window_(window),path_(path) {
        RECT area;GetClientRect(window_,&area);renderer_=std::make_unique<gm::Renderer>(window_,area.right,area.bottom);
        if(path.extension()==".gmscene") {scene_=std::make_unique<gm::SceneRuntime>(gm::loadScene(path));sceneView_=std::make_unique<gm::SceneView>(*renderer_);ImmAssociateContextEx(window_,nullptr,0);}else document_=std::make_unique<gm::RuntimeDocument>(path);
    }
    ~Player(){sceneView_.reset();view_.clear(*renderer_);}
    bool needsFrame() const {return (redraw_||(scene_&&GetForegroundWindow()==window_)) && !IsIconic(window_);}
    DWORD waitTime() const {return scene_?16:INFINITE;}
    void requestFrame() {redraw_=true;}
    void key(WPARAM key){if(!scene_)return;if(key=='D'||key==VK_RIGHT)pending_.x=1;if(key=='A'||key==VK_LEFT)pending_.x=-1;if(key=='S'||key==VK_DOWN)pending_.y=1;if(key=='W'||key==VK_UP)pending_.y=-1;}
    void frame() {
        RECT area;GetClientRect(window_,&area);if(area.right<1 || area.bottom<1) return;
        if(scene_){auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-lastTick_).count();lastTick_=now;gm::SceneInput input;
            if(GetForegroundWindow()==window_){auto held=[](int k){return (GetAsyncKeyState(k)&0x8000)!=0;};input={float(held('D')||held(VK_RIGHT))-float(held('A')||held(VK_LEFT)),float(held('S')||held(VK_DOWN))-float(held('W')||held(VK_UP))};if(input.x==0)input.x=pending_.x;if(input.y==0)input.y=pending_.y;pending_={};scene_->tick(dt,input);}
            renderer_->begin(area.right,area.bottom);sceneView_->render(*scene_,{0,0,float(area.right),float(area.bottom)});renderer_->end();redraw_=false;return;}
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
        if(scene_){auto node=pressed_;pressed_=gm::NoIndex;ReleaseCapture();if(node!=gm::NoIndex&&sceneView_->hitUi(*scene_,x,y)==node)scene_->activateUi(node);return;}
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
    void cancel() {pressed_=gm::NoIndex;requestFrame();}
    void reload() {if(scene_){auto next=std::make_unique<gm::SceneRuntime>(gm::loadScene(path_));sceneView_->clear();scene_=std::move(next);lastTick_=std::chrono::steady_clock::now();}else document_->reload();cancel();ReleaseCapture();requestFrame();}
private:
    void updateLayout() {RECT area;GetClientRect(window_,&area);if(area.right>0 && area.bottom>0) view_.layout.update(*document_,float(area.right),float(area.bottom));}
    HWND window_;std::filesystem::path path_;std::unique_ptr<gm::RuntimeDocument> document_;gm::RuntimeView view_;
    std::unique_ptr<gm::Renderer> renderer_;
    std::unique_ptr<gm::SceneRuntime> scene_;std::unique_ptr<gm::SceneView> sceneView_;
    std::chrono::steady_clock::time_point lastTick_=std::chrono::steady_clock::now();
    gm::SceneInput pending_{};
    uint32_t pressed_=gm::NoIndex;float mouseX_=-1,mouseY_=-1;bool redraw_=true;
};
Player* player=nullptr;
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    try {
        if(message==WM_GETMINMAXINFO) {reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={480,340};return 0;}
        if(message==WM_PAINT) {PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);if(player) player->requestFrame();return 0;}
        if(player) switch(message) {
            case WM_SIZE:player->requestFrame();return 0;
            case WM_LBUTTONDOWN:SetFocus(window);player->down(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_LBUTTONUP:player->up(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_MOUSEMOVE:player->move(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_MOUSELEAVE:player->leave();return 0;
            case WM_CAPTURECHANGED:player->cancel();return 0;
            case WM_KEYDOWN:if(wp==VK_F5) {player->reload();return 0;}player->key(wp);break;
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
        RECT bounds{0,0,960,600};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
        const auto title=project.filename().wstring()+L" — GM Engine Player";
        HWND window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
        if(!window) throw std::runtime_error("Cannot create player window");
        Player instancePlayer(window,project);player=&instancePlayer;ShowWindow(window,SW_SHOW);UpdateWindow(window);
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
