#include "document.hpp"
#include "editor_documents.hpp"
#include "ui_compiler.hpp"
#include "renderer.hpp"
#include "render_diagnostics.hpp"
#include "ui_runtime.hpp"
#include "authoring_images.hpp"
#include "editor_layout.hpp"
#include "editor_default.hpp"
#include "scene_compiler.hpp"
#include "scene_view.hpp"
#include "windows_game_input.hpp"
#include "project.hpp"
#include "project_tasks.hpp"
#include <functional>
#include <unordered_map>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <imm.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>

using gm::Json;
using gm::Rect;
namespace {
uint32_t C(const char* hex) { return gm::color(hex); }
struct Hit { Rect rect; Json command; std::string tooltip; bool enabled=true; };
struct Field { Rect rect; std::string property; };
struct NodeHit { Rect rect; std::string id; };
struct Edit { std::string node,property; std::wstring buffer,original; size_t cursor=0,anchor=0; uint64_t revision=0; };
struct FileItem { std::filesystem::path path; std::string label; int depth; bool folder; };
std::string nameOf(const Json& node) { return node.value("name",node.at("id").get<std::string>()); }
std::string kindName(const std::string& type) {
    if(type=="image") return "图片";if(type=="list")return "滚动列表";
    static const std::map<std::string,std::string> names={{"panel","面板"},{"button","按钮"},{"label","文本"},{"tree","场景面板"},{"files","文件系统"},{"viewport","视口"},{"inspector","检查器"},{"toolbar","工具栏"},{"menubar","菜单栏"},{"output","输出"},{"status","状态栏"}};
    auto it=names.find(type); return it==names.end()?type:it->second;
}
bool matches(std::string text,std::string term) {
    auto lower=[](unsigned char c){ return char(std::tolower(c)); };
    std::transform(text.begin(),text.end(),text.begin(),lower); std::transform(term.begin(),term.end(),term.begin(),lower);
    return text.find(term)!=std::string::npos;
}
std::string pathText(const std::filesystem::path& path) { return gm::utf8(path.wstring()); }

class Editor {
public:
    Editor(HWND window,std::filesystem::path path,std::filesystem::path session,gm::GameProject project)
        :window_(window),doc_(std::move(path)),gameProject_(std::move(project)),session_(std::move(session)),root_(gm::engineRoot()) {
        shell_=loadShell();
        shellDirectory_=root_/"editor";
        std::filesystem::create_directories(session_/"inbox"); std::filesystem::create_directories(session_/"outbox");
        lock_=CreateFileW((session_/"editor.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lock_==INVALID_HANDLE_VALUE) throw std::runtime_error("This editor session is already open.");
        for(const auto& f:std::filesystem::directory_iterator(session_/"inbox")) if(f.path().extension()==".json") {
            gm::writeJsonAtomic(session_/"outbox"/f.path().filename(),{{"ok",false},{"error","Editor restarted; request was not executed"}});
            std::filesystem::remove(f.path());
        }
        updateSession(); loadWorkspace(); refreshFiles();
        RECT client; GetClientRect(window_,&client);
        renderer_=std::make_unique<gm::Renderer>(window_,client.right,client.bottom);
        images_=std::make_unique<gm::AuthoringImages>(*renderer_);
        if(sceneMode()){for(const auto& e:doc_.data()["entities"])if(gm::findComponent(e,"Velocity")){doc_.execute({{"op","select"},{"id",e["id"]}});break;}selectedFolder_=doc_.path().parent_path();groups_.insert("Velocity");}
        else if(gm::findNode(doc_.data()["root"],"button")) doc_.execute({{"op","select"},{"id","button"}});
        logs_.push_back("已打开 " + pathText(doc_.path().filename()));
    }
    ~Editor() {
        if(projectBusy()){projectTask_.cancel();pollProjectTask(true);}
        stopPreview();
        saveWorkspace(); edit3DView_.reset();edit3D_.reset(); images_.reset(); renderer_.reset(); std::error_code ec;
        std::filesystem::remove(session_/"session.json",ec);
        if(lock_!=INVALID_HANDLE_VALUE) CloseHandle(lock_);
    }
    Json command(const Json& request) {
        requestFrame();
        try {
            const auto op=request.at("op").get<std::string>();
            if(projectBusy()&&!allowedDuringProjectTask(op))throw std::runtime_error("项目任务执行中；可继续查看，或在项目面板取消任务后编辑。");
            if(request.contains("expected_revision")&&request["expected_revision"]!=doc_.revision())throw std::runtime_error("Revision conflict; query again");
            if(scenePlay_&&(op=="set"||op=="batch"||op=="undo"||op=="redo"||op=="reload"||op=="create"||op=="remove"||op=="duplicate"||op=="asset.add"||op=="asset.apply"||op=="system.set"||op.starts_with("binding.")||op.starts_with("entity.")||op.starts_with("component.")))throw std::runtime_error("请先停止运行，再编辑场景组件。");
            if(op.starts_with("project."))return projectCommand(request);
            if(op=="dialog.project.open"||op=="dialog.project.new"){projectDialog(op=="dialog.project.new");return state();}
            if(op=="view.inspector"){auto page=request.at("page").get<std::string>();if(page!="components"&&page!="systems"&&page!="project"&&page!="profiler")throw std::runtime_error("Unknown inspector page");if(page=="systems"&&!sceneMode())throw std::runtime_error("Open a scene to edit Systems");inspectorPage_=page;assetInspection_=false;inspectorScroll_=0;return state();}
            if(op=="view.scene"){sceneGameView_=request.at("game").get<bool>()&&scenePlay_;setGameFocus(false);return state();}
            if(op=="runtime.render.stats")return {{"ok",true},{"result",gm::renderDiagnostics(renderer_->stats())}};
            if(op=="runtime.query")return sceneRuntimeState();
            if(op.starts_with("runtime.")&&op!="runtime.step"&&op!="runtime.activate"){if(!scenePlay_)throw std::runtime_error("Scene is not running");auto result=gm::runtimeCommand(*scenePlay_,request,gameProject_);if(op=="runtime.load"||op=="runtime.scene.switch")sceneView_->clear();requestFrame();return result;}
            if(op=="preview.pause"){if(!scenePlay_)throw std::runtime_error("Scene is not running");scenePaused_=request.value("paused",!scenePaused_);scenePlay_->resetClock();return sceneRuntimeState();}
            if(op=="preview.step"||op=="runtime.step"){if(!scenePlay_||!scenePaused_)throw std::runtime_error("Pause the scene before stepping");auto frames=request.value("frames",1);if(frames<1||frames>10000)throw std::runtime_error("frames must be 1..10000");for(int i=0;i<frames;++i)scenePlay_->tick(1.f/60,{request.value("x",0.f),request.value("y",0.f)});return sceneRuntimeState();}
            if(op=="runtime.activate"){if(!scenePlay_)throw std::runtime_error("Scene is not running");if(!scenePlay_->activateUi(request.at("node").get<uint32_t>(),request.value("row",-1)))throw std::runtime_error("UI action not found");return sceneRuntimeState();}
            if(request.contains("expected_revision") && request["expected_revision"]!=doc_.revision()) throw std::runtime_error("Revision conflict; query again");
            if(op=="menu") { showMenu(request.at("name").get<std::string>()); return state(); }
            if(op=="dialog.open") { openDialog(false); return state(); }
            if(op=="dialog.new") { openDialog(true); return state(); }
            if(op=="dialog.scene") { openDialog(true,true); return state(); }
            if(op=="dialog.color") { chooseColor(request.at("property").get<std::string>()); return state(); }
            if(op=="dialog.image") { if(sceneMode())sceneImageDialog(request.value("type",std::string("SpriteRenderer")),request.value("field",std::string("image")));else chooseImage(); return state(); }
            if(op=="tree.toggle") { auto id=request.at("id").get<std::string>(); if(!collapsed_.erase(id)) collapsed_.insert(id); return state(); }
            if(op=="group.toggle") { auto key=request.at("name").get<std::string>(); if(!groups_.erase(key)) groups_.insert(key); return state(); }
            if(op=="files.refresh") { edit3DRevision_=UINT64_MAX; images_->clear();refreshFiles(); return state(); }
            if(op=="files.select") { selectedFile_=std::filesystem::path(gm::wide(request.at("path").get<std::string>()));assetInspection_=true;inspectorScroll_=0; return state(); }
            if(op=="files.folder") { selectedFolder_=std::filesystem::path(gm::wide(request.at("path").get<std::string>()));fileScroll_=0;fileSearch_.clear();return state(); }
            if(op=="asset.add" || op=="asset.apply") { useImage(op=="asset.apply");return state(); }
            if(op=="tab.select") { if(commit()) {doc_.activate(request.at("index").get<size_t>());resetView();}return state(); }
            if(op=="tab.close") { if(commit() && (!request.value("interactive",false) || confirmSwitch())) {doc_.close(request.value("interactive",false)||request.value("discard",false));resetView();}return state(); }
            if(op=="preview.run") { if(commit()){runPreview();if(scenePlay_)scenePaused_=request.value("paused",false);}return state(); }
            if(op=="preview.stop") {stopPreview();message_="已停止运行 · 已回到编辑状态";return state();}
            if(op=="file.open") {
                auto path=std::filesystem::path(gm::wide(request.at("path").get<std::string>()));
                if(path.is_relative())path=contentRoot()/path;
                if(commit()) switchDocument(path,true);
                return state();
            }
            if(op=="open-editor") { if(commit()) switchDocument(root_/"editor/editor.ui.json",true); return state(); }
            if(op=="view.fit") { zoom_=1;panX_=panY_=0; return state(); }
            if(op=="view.zoom") { zoomAt(canvasArea_.x+canvasArea_.w*.5f,canvasArea_.y+canvasArea_.h*.5f,request.at("factor").get<float>()); return state(); }
            if(op=="view.grid") { grid_=!grid_; return state(); }
            if(op=="view.tool") { auto tool=request.at("tool").get<std::string>();if(tool!="select" && tool!="move") throw std::runtime_error("Unknown viewport tool");tool_=tool;return state(); }
            if(op=="output.toggle") { layout_.outputOpen=!layout_.outputOpen; return state(); }
            if(op=="bottom.tab") {layout_.outputOpen=request.at("name")=="output";return state();}
            if(op=="output.clear") { logs_.clear(); errorCount_=0; return state(); }
            if(op=="layout.reset") { layout_=gm::WorkspaceLayout{}; zoom_=1;panX_=panY_=0; return state(); }
            if(op=="apply") {
                if(sceneMode())throw std::runtime_error("请选择编辑器 UI 资源。");
                bool hasDock=false;
                if(doc_.data()["root"].contains("children")) for(const auto& node:doc_.data()["root"]["children"]) if(node.contains("dock")) hasDock=true;
                if(!hasDock) throw std::runtime_error("当前资源不是编辑器界面。请先从工具菜单打开编辑器界面资源。");
                shell_=doc_.data();shellDirectory_=doc_.path().parent_path();images_->clear(); message_="编辑器界面已应用"; return state();
            }
            if(op=="recover") { shell_=Json::parse(kDefaultEditor); layout_=gm::WorkspaceLayout{}; message_="默认布局已恢复"; return state(); }
            if(op=="add-button") return command({{"op","create"},{"type","button"}});
            if(op=="describe") {
                auto response=doc_.execute(request);
                gm::addProjectCommands(response["result"]);response["result"]["project"]=gm::projectDescription(gameProject_);
                auto& commands=response["result"]["commands"];
                commands["file.open"]={{"path","UTF-8 resource path; opens or activates a tab"}};
                commands["tab.select"]={{"index","zero-based tab index"}};commands["tab.close"]={{"discard","optional boolean; default rejects unsaved changes"}};
                commands["files.folder"]={{"path","folder path"}};commands["files.select"]={{"path","asset path"}};
                commands["asset.add"]="Add the selected image to the canvas";commands["asset.apply"]="Apply the selected image to the selected image node";
                commands["bottom.tab"]={{"name","assets or output"}};commands["files.refresh"]="Refresh asset files and image cache";
                commands["preview.run"]="Compile current in-memory resource; scene runs in isolated World (optional paused:true), UI launches Player";commands["preview.stop"]="Stop simulation or this editor's UI preview process";
                commands["project.task.status"]="Live editor: current or last task ID, state, elapsed time and final response";
                commands["project.task.cancel"]="Live editor: cooperatively cancel the active task and its build processes";
                commands["project.build"]="Stop and save first; default live CLI waits for final result; async:true returns a task ID";
                commands["project.package"]="Package saved assets in background; optional async:true in live CLI";
                commands["project.assets"]="Import saved assets in background; stop and save first; optional async:true in live CLI";
                commands["apply"]="Apply an editor UI resource to the GUI shell";
                commands["recover"]="Restore the GUI shell; leaves current document unchanged";
                commands["layout.reset"]="Reset GUI panel sizes";
                commands["view.fit"]="Fit canvas in viewport"; commands["view.zoom"]={{"factor","positive zoom factor"}};
                commands["view.grid"]="Toggle viewport grid"; commands["view.tool"]={{"tool","select or move"}};
                commands["preview.pause"]="Pause/resume a scene; optional paused boolean";commands["preview.step"]="Advance a paused scene by 1/60 second";commands["runtime.query"]="Inspect runtime components and HUD";commands["runtime.step"]={{"frames","1..10000 fixed 1/60 steps"},{"x","optional input -1..1"},{"y","optional input -1..1"}};commands["runtime.activate"]={{"node","compiled HUD button index"}};
                return response;
            }
            auto response=doc_.execute(request);
            if(op=="query") {response["tabs"]=Json::array();for(const auto& tab:doc_.tabs()) response["tabs"].push_back({{"path",pathText(tab->path())},{"dirty",tab->dirty()}});response["active_tab"]=doc_.active();}
            if(!response.value("ok",false)) fail(response.value("error","操作失败"));
            else {
                if(op=="select" || op=="create" || op=="duplicate" || op=="entity.create" || op=="entity.duplicate") {assetInspection_=false;inspectorPage_="components";inspectorScroll_=0;}
                error_=false;
                if(op=="save") { message_="已保存 " + pathText(doc_.path().filename()); logs_.push_back(message_); }
                else if(op=="reload") { images_->clear();message_="资源已重新加载"; logs_.push_back(message_); }
                else if(op=="undo") message_="已撤销";
                else if(op=="redo") message_="已重做";
                else if(op=="create") message_="已创建节点";
                else if(op=="remove") message_="已删除节点";
                else if(op=="set" || op=="batch") message_.clear();
            }
            return response;
        } catch(const std::exception& e) { auto response=state(); response["ok"]=false;response["error"]=e.what();fail(e.what());return response; }
    }
    void frame() {
        pollProjectTask();
        poll();
        updateScenePlay();
        if(previewProcess_ && WaitForSingleObject(previewProcess_,0)==WAIT_OBJECT_0) {CloseHandle(previewProcess_);previewProcess_=nullptr;requestFrame();}
        if(tooltipPending_ && std::chrono::steady_clock::now()-hoverTime_>=std::chrono::milliseconds(850)) {tooltipPending_=false;requestFrame();}
        if(IsIconic(window_) || !redraw_) return;
        redraw_=false;
        RECT client; GetClientRect(window_,&client); if(client.right<1 || client.bottom<1) return;
        scale_=float(GetDpiForWindow(window_))/96.f; width_=float(client.right)/scale_;height_=float(client.bottom)/scale_;
        layout_.arrange(width_,height_);
        renderer_->begin(client.right,client.bottom); hits_.clear();fields_.clear();nodes_.clear();previewRect_={};canvasArea_={};
        auto arranged=shell_; arranged["root"]["w"]=width_; arranged["root"]["h"]=height_;
        if(arranged["root"].contains("children")) for(auto& node:arranged["root"]["children"]) {
            const auto dock=node.value("dock",std::string("none")); auto found=layout_.regions.find(dock);
            if(node["type"]=="output") {node["visible"]=false;continue;}
            if(node["type"]=="files") found=layout_.regions.find("bottom");
            if(found!=layout_.regions.end()) { auto r=found->second; node["x"]=r.x;node["y"]=r.y;node["w"]=r.w;node["h"]=r.h; }
        }
        drawDocument(arranged["root"],0,0,scale_,false,{0,0,float(client.right),float(client.bottom)});
        renderer_->unclip(); drawSplitters(); drawTooltip(); renderer_->end();
        auto title=gm::wide(pathText(doc_.path().filename())+(doc_.dirty()?" *":"")+" — GM Engine");
        if(title!=lastTitle_) { SetWindowTextW(window_,title.c_str());lastTitle_=title; }
    }
    void gameKey(uint32_t key,bool repeat){if(gameFocus_)gameInput_.key(key,repeat);}
    bool gameTouch(WPARAM p,bool down,bool began){
        POINTER_INFO info{};if(!GetPointerInfo(GET_POINTERID_WPARAM(p),&info)||info.pointerType!=PT_TOUCH)return false;
        if(began){auto point=info.ptPixelLocation;ScreenToClient(window_,&point);setGameFocus(scenePlay_&&sceneGameView_&&canvasArea_.contains(float(point.x),float(point.y)));}
        if(!gameFocus_)return false;requestFrame();return gameInput_.pointer(window_,p,down);
    }
    bool gameRight(bool down){if(gameFocus_)gameInput_.button(2,down);return gameFocus_;}
    void gameBlur(){gameInput_.clear();}
    void mouseDown(float x,float y,bool doubleClick=false,bool middle=false) {
        requestFrame();
        mouseX_=x;mouseY_=y;
        setGameFocus(scenePlay_&&sceneGameView_&&canvasArea_.contains(x,y));
        if(gameFocus_){gameInput_.button(middle?4:1,true);SetCapture(window_);scenePressed_=sceneView_->hitUi(*scenePlay_,x,y);if(scenePressed_!=gm::NoIndex)SetCapture(window_);return;}
        if(middle && canvasArea_.contains(x,y)) { if(!commit()) return; dragMode_="pan";startX_=x;startY_=y;oldPanX_=panX_;oldPanY_=panY_;SetCapture(window_);return; }
        for(auto& [name,r]:splitters()) if(r.contains(x,y)) { if(!commit()) return;dragMode_=name;SetCapture(window_);return; }
        for(auto it=fields_.rbegin();it!=fields_.rend();++it) if(it->rect.contains(x,y)) {
            if(!commit()) return;
            startEdit(it->property);
            HIMC context=ImmGetContext(window_); COMPOSITIONFORM form{};form.dwStyle=CFS_POINT;form.ptCurrentPos={LONG(x),LONG(y+26*scale_)};ImmSetCompositionWindow(context,&form);ImmReleaseContext(window_,context);return;
        }
        if(!commit()) return;
        for(auto it=hits_.rbegin();it!=hits_.rend();++it) if(it->rect.contains(x,y)) {
            if(!it->enabled) return;
            auto cmd=it->command;
            if(doubleClick && cmd.value("op",std::string())=="files.select" && (cmd["path"].get<std::string>().ends_with(".ui.json") || cmd["path"].get<std::string>().ends_with(".scene.json"))) { cmd["op"]="file.open";cmd["interactive"]=true; }
            command(cmd); return;
        }
        if(canvasArea_.contains(x,y)) {
            if(sceneMode()) {if(scenePlay_)return;for(auto it=nodes_.rbegin();it!=nodes_.rend();++it)if(it->rect.contains(x,y)){command({{"op","select"},{"id",it->id}});auto* e=selectedNode();auto* t=gm::findComponent(*e,"Transform2D");if(t&&tool_=="move"){dragMode_="scene-node";dragId_=it->id;dragRevision_=doc_.revision();startX_=x;startY_=y;nodeX_=t->at("fields")["x"].get<float>();nodeY_=t->at("fields")["y"].get<float>();dx_=dy_=0;SetCapture(window_);}return;}return;}
            for(auto it=nodes_.rbegin();it!=nodes_.rend();++it) {
                Rect resize{it->rect.x+it->rect.w-7*scale_,it->rect.y+it->rect.h-7*scale_,14*scale_,14*scale_};
                bool handle=it->id==doc_.selected() && resize.contains(x,y);
                if(it->rect.contains(x,y) || handle) {
                    command({{"op","select"},{"id",it->id}});
                    if((tool_=="move" || handle) && it->id!=rootId()) {
                        const auto* node=gm::findNode(doc_.data()["root"],it->id);
                        dragMode_=handle?"resize":"node";dragId_=it->id;dragRevision_=doc_.revision();
                        startX_=x;startY_=y;nodeX_=(*node)["x"].get<float>();nodeY_=(*node)["y"].get<float>();nodeW_=(*node)["w"].get<float>();nodeH_=(*node)["h"].get<float>();dx_=dy_=0;SetCapture(window_);
                    }
                    return;
                }
            }
        }
    }
    void mouseMove(float x,float y) {
        if(x!=mouseX_ || y!=mouseY_) requestFrame();
        mouseX_=x;mouseY_=y;
        if(dragMode_=="node" || dragMode_=="resize" || dragMode_=="scene-node") {auto delta=gm::Transform2D{(x-startX_)/previewScale_,(y-startY_)/previewScale_};if(dragMode_=="scene-node")delta=sceneLocalDelta(delta.x,delta.y);dx_=std::round(delta.x);dy_=std::round(delta.y);}
        else if(dragMode_=="pan") { panX_=oldPanX_+x-startX_;panY_=oldPanY_+y-startY_; }
        else if(dragMode_=="left") layout_.left=x/scale_;
        else if(dragMode_=="right") layout_.right=width_-x/scale_;
        else if(dragMode_=="tree") layout_.treeFraction=(y/scale_-62)/(height_-84);
        else if(dragMode_=="output") layout_.outputHeight=height_-22-y/scale_;
        const auto now=std::chrono::steady_clock::now(); if(std::abs(x-hoverX_)>2 || std::abs(y-hoverY_)>2) { hoverX_=x;hoverY_=y;hoverTime_=now;tooltipPending_=true; }
        setCursor();
    }
    void mouseUp() {
        if(gameFocus_){gameInput_.button(1,false);gameInput_.button(4,false);}
        requestFrame();
        if(scenePlay_&&scenePressed_!=gm::NoIndex){scenePressed_=gm::NoIndex;ReleaseCapture();return;}
        const auto mode=dragMode_;dragMode_.clear();ReleaseCapture();
        if(mode=="scene-node"&&(dx_!=0||dy_!=0)){command({{"op","batch"},{"expected_revision",dragRevision_},{"commands",Json::array({{{"op","set"},{"id",dragId_},{"property","Transform2D.x"},{"value",nodeX_+dx_}},{{"op","set"},{"id",dragId_},{"property","Transform2D.y"},{"value",nodeY_+dy_}}})}});return;}
        if((mode=="node" || mode=="resize") && (dx_!=0 || dy_!=0)) {
            const auto xkey=mode=="resize"?"w":"x",ykey=mode=="resize"?"h":"y";
            float a=mode=="resize"?std::max(1.f,nodeW_+dx_):nodeX_+dx_,b=mode=="resize"?std::max(1.f,nodeH_+dy_):nodeY_+dy_;
            command({{"op","batch"},{"expected_revision",dragRevision_},{"commands",Json::array({{{"op","set"},{"id",dragId_},{"property",xkey},{"value",a}},{{"op","set"},{"id",dragId_},{"property",ykey},{"value",b}}})}});
        }
        if(mode=="left" || mode=="right" || mode=="tree" || mode=="output") saveWorkspace();
    }
    void wheel(int delta,float x,float y) {
        if(gameFocus_&&canvasArea_.contains(x,y))gameInput_.wheel(float(delta)/120.f);
        requestFrame();
        if(canvasArea_.contains(x,y)) {if(!scenePlay_)zoomAt(x,y,delta>0?1.12f:1/1.12f);}
        else if(px(layout_.regions["left-top"]).contains(x,y)) treeScroll_=std::max(0,treeScroll_+(delta>0?-3:3));
        else if(px(layout_.regions["bottom"]).contains(x,y)) {
            const auto bottom=px(layout_.regions["bottom"]);auto& offset=x<bottom.x+std::min(180*scale_,bottom.w*.26f)?folderScroll_:fileScroll_;
            offset=std::max(0,offset+(delta>0?-1:1));
        }
        else if(px(layout_.regions["right"]).contains(x,y)) inspectorScroll_=std::max(0.f,inspectorScroll_-float(delta)*45.f/120.f);
    }
    void eraseSelection() {
        if(!edit_) return;
        const auto a=std::min(edit_->cursor,edit_->anchor),b=std::max(edit_->cursor,edit_->anchor);
        edit_->buffer.erase(a,b-a);edit_->cursor=edit_->anchor=a;
    }
    void input(wchar_t ch) {
        if(!edit_ || ch<32) return;
        requestFrame();eraseSelection();
        if(edit_->buffer.size()<2048) {edit_->buffer.insert(edit_->cursor,1,ch);++edit_->cursor;edit_->anchor=edit_->cursor;}updateSearch();
    }
    void ime(const std::wstring& text) { for(auto ch:text) input(ch); }
    void key(WPARAM key) {
        requestFrame();
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(key==VK_ESCAPE) {setGameFocus(false);if(edit_) {edit_->buffer=edit_->original;updateSearch();} edit_.reset();dragMode_.clear();ReleaseCapture();return; }
        if(ctrl && key=='S') { if(commit()) command({{"op","save"}});return; }
        if(ctrl && key=='O') { if(commit()) openDialog(false);return; }
        if(ctrl && key=='N') { if(commit()) openDialog(true,sceneMode());return; }
        if(key==VK_F7&&scenePlay_){command({{"op",shift?"preview.step":"preview.pause"}});return;}
        if(scenePlay_&&gameFocus_&&!ctrl&&key!=VK_F8&&key!=VK_F5){if(key=='D'||key==VK_RIGHT)pendingSceneInput_.x=1;if(key=='A'||key==VK_LEFT)pendingSceneInput_.x=-1;if(key=='S'||key==VK_DOWN)pendingSceneInput_.y=1;if(key=='W'||key==VK_UP)pendingSceneInput_.y=-1;return;}
        if(key==VK_F6) { edit_.reset();command({{"op","recover"}});return; }
        if(edit_) {
            if(key==VK_RETURN) {commit();return;}
            if(key==VK_TAB) {
                auto property=edit_->property;std::vector<std::string> keys;
                for(const auto& field:fields_) if(field.rect.h>0) keys.push_back(field.property);
                const auto it=std::find(keys.begin(),keys.end(),property);
                if(commit() && it!=keys.end() && !keys.empty()) startEdit(keys[(size_t(it-keys.begin())+keys.size()+(shift?-1:1))%keys.size()]);return;
            }
            if(ctrl && key=='A') {edit_->anchor=0;edit_->cursor=edit_->buffer.size();return;}
            if(ctrl && (key=='C' || key=='X')) {
                auto text=edit_->buffer.substr(std::min(edit_->cursor,edit_->anchor),std::max(edit_->cursor,edit_->anchor)-std::min(edit_->cursor,edit_->anchor));
                if(!text.empty() && OpenClipboard(window_)) {
                    auto h=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
                    if(h) {auto* data=GlobalLock(h);if(data) {memcpy(data,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(h);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,h)) GlobalFree(h);}else GlobalFree(h);}CloseClipboard();
                    if(key=='X') {eraseSelection();updateSearch();}
                }return;
            }
            if(key==VK_LEFT || key==VK_RIGHT || key==VK_HOME || key==VK_END) {
                if(key==VK_HOME) edit_->cursor=0;
                else if(key==VK_END) edit_->cursor=edit_->buffer.size();
                else if(!shift && edit_->cursor!=edit_->anchor) edit_->cursor=key==VK_LEFT?std::min(edit_->cursor,edit_->anchor):std::max(edit_->cursor,edit_->anchor);
                else if(key==VK_LEFT && edit_->cursor) {--edit_->cursor;if(edit_->cursor && edit_->buffer[edit_->cursor]>=0xdc00 && edit_->buffer[edit_->cursor]<=0xdfff) --edit_->cursor;}
                else if(key==VK_RIGHT && edit_->cursor<edit_->buffer.size()) {++edit_->cursor;if(edit_->cursor<edit_->buffer.size() && edit_->buffer[edit_->cursor]>=0xdc00 && edit_->buffer[edit_->cursor]<=0xdfff) ++edit_->cursor;}
                if(!shift) edit_->anchor=edit_->cursor;return;
            }
            if(key==VK_BACK || key==VK_DELETE) {
                if(edit_->cursor==edit_->anchor) {
                    if(key==VK_BACK && edit_->anchor) {--edit_->anchor;if(edit_->anchor && edit_->buffer[edit_->anchor]>=0xdc00 && edit_->buffer[edit_->anchor]<=0xdfff) --edit_->anchor;}
                    else if(key==VK_DELETE && edit_->anchor<edit_->buffer.size()) {++edit_->anchor;if(edit_->anchor<edit_->buffer.size() && edit_->buffer[edit_->anchor]>=0xdc00 && edit_->buffer[edit_->anchor]<=0xdfff) ++edit_->anchor;}
                }eraseSelection();updateSearch();return;
            }
            if(ctrl && key=='V' && OpenClipboard(window_)) {
                auto h=GetClipboardData(CF_UNICODETEXT);if(h) {const auto* p=static_cast<const wchar_t*>(GlobalLock(h));if(p){ime(p);GlobalUnlock(h);}}CloseClipboard();return;
            }
            return;
        }
        if(ctrl && key==VK_TAB) {command({{"op","tab.select"},{"index",(doc_.active()+doc_.tabs().size()+(shift?-1:1))%doc_.tabs().size()}});return;}
        if(ctrl && key=='W') {if(doc_.tabs().size()>1) command({{"op","tab.close"},{"interactive",true}});return;}
        if(ctrl && key=='D') {if(!assetInspection_) command({{"op","duplicate"},{"id",doc_.selected()}});return;}
        if(key==VK_F8) {command({{"op","preview.stop"}});return;}

        if(ctrl && key=='Z') command({{"op","undo"}});
        else if(ctrl && key=='Y') command({{"op","redo"}});
        else if(key==VK_DELETE && !assetInspection_ && doc_.selected()!=rootId()) command({{"op","remove"},{"id",doc_.selected()}});
        else if(key==VK_F2 && !assetInspection_) startEdit("name");
        else if(key=='F') command({{"op","view.fit"}});
        else if(key=='Q') tool_="select";
        else if(key=='W') tool_="move";
        else if(key==VK_F5) command({{"op",ctrl&&shift?"apply":"preview.run"}});
        else if(key==VK_LEFT || key==VK_RIGHT || key==VK_UP || key==VK_DOWN) {
            if(assetInspection_) return;
            if(sceneMode()){if(scenePlay_)return;auto* e=selectedNode();if(!e)return;auto* c=gm::findComponent(*e,"Transform2D");if(!c)return;auto field=(key==VK_LEFT||key==VK_RIGHT)?"x":"y";float step=(shift?10.f:1.f)*((key==VK_LEFT||key==VK_UP)?-1.f:1.f);command({{"op","set"},{"id",doc_.selected()},{"property",std::string("Transform2D.")+field},{"value",c->at("fields")[field].get<float>()+step}});return;}
            if(doc_.selected()==rootId()) return;
            auto* node=selectedNode();const auto property=(key==VK_LEFT||key==VK_RIGHT)?"x":"y";
            float step=(GetKeyState(VK_SHIFT)&0x8000)?10.f:1.f;if(key==VK_LEFT||key==VK_UP) step=-step;
            command({{"op","set"},{"id",doc_.selected()},{"property",property},{"value",node->at(property).get<float>()+step}});
        }
    }
    bool close() {
        if(!commit()) return false;const auto active=doc_.active();
        for(size_t i=0;i<doc_.tabs().size();++i) {doc_.activate(i);if(!confirmSwitch()) {doc_.activate(active);requestFrame();return false;}}
        return true;
    }
    void requestFrame() {redraw_=true;}
    DWORD idleWait() const {return scenePlay_&&!scenePaused_&&GetForegroundWindow()==window_?16:100;}
    void context(float x,float y) {
        mouseX_=x;mouseY_=y;requestFrame();if(!commit()) return;
        for(auto it=hits_.rbegin();it!=hits_.rend();++it) if(it->rect.contains(x,y)) {
            auto op=it->command.value("op",std::string());
            if(op=="select") {command(it->command);showMenu("node");return;}
            if(op=="files.select") {command(it->command);showMenu("asset");return;}
        }
        if(canvasArea_.contains(x,y)) {for(auto it=nodes_.rbegin();it!=nodes_.rend();++it) if(it->rect.contains(x,y)) {command({{"op","select"},{"id",it->id}});break;}showMenu("node");}
    }
    void captureLost() { dragMode_.clear();gameInput_.cancelButtons(); }
private:
    #include "editor_tasks.inl"
    #include "editor_project.inl"
    #include "editor_scene.inl"
    Json state() { auto response=doc_.execute({{"op","query"}});response["result"]=nullptr;return response; }
    void fail(const std::string& message) { message_=message;error_=true;++errorCount_;logs_.push_back(message);layout_.outputOpen=true;requestFrame(); }
    Json loadShell() { try { auto shell=gm::readJson(root_/"editor/editor.ui.json");gm::validateDocument(shell);return shell;} catch(...) {return Json::parse(kDefaultEditor);} }
    void updateSession() { gm::writeJsonAtomic(session_/"session.json",{{"protocol","gm.editor/1"},{"pid",GetCurrentProcessId()},{"project",pathText(doc_.path())}}); }
    void loadWorkspace() {
        try { auto j=gm::readJson(session_.parent_path()/"workspace.json");layout_.left=j.value("left",228.f);layout_.right=j.value("right",300.f);layout_.treeFraction=j.value("tree",.58f);layout_.outputHeight=j.value("outputHeight",180.f);
            if(!std::isfinite(layout_.left)||!std::isfinite(layout_.right)||!std::isfinite(layout_.treeFraction)||!std::isfinite(layout_.outputHeight)) layout_=gm::WorkspaceLayout{};
        } catch(...) {}
    }
    void saveWorkspace() { try { gm::writeJsonAtomic(session_.parent_path()/"workspace.json",{{"left",layout_.left},{"right",layout_.right},{"tree",layout_.treeFraction},{"outputHeight",layout_.outputHeight}}); } catch(...) {} }
    void poll() {
        std::vector<std::filesystem::path> files;
        for(const auto& f:std::filesystem::directory_iterator(session_/"inbox")) if(f.path().extension()==".json") files.push_back(f.path());
        std::sort(files.begin(),files.end());
        for(size_t i=0;i<std::min<size_t>(files.size(),16);++i) {
            if(files[i]==deferredProjectRequest_)continue;
            Json request,response;try {request=gm::readJson(files[i]);response=command(request);} catch(const std::exception& e) {response={{"ok",false},{"error",e.what()}};}
            if(response.value("pending",false)&&!request.value("async",false)){deferredProjectRequest_=files[i];continue;}
            gm::writeJsonAtomic(session_/"outbox"/files[i].filename(),response);std::filesystem::remove(files[i]);
        }
    }
    bool confirmSwitch() {
        if(!doc_.dirty()) return true;
        const auto prompt=gm::wide("保存对 "+pathText(doc_.path().filename())+" 的修改吗？");
        int response=MessageBoxW(window_,prompt.c_str(),L"未保存的修改",MB_YESNOCANCEL|MB_ICONQUESTION);
        return response==IDNO || (response==IDYES && command({{"op","save"}}).value("ok",false));
    }
    void resetView() {
        stopPreview();
        edit_.reset();collapsed_.clear();treeScroll_=0;inspectorScroll_=0;zoom_=1;panX_=panY_=0;treeSearch_.clear();assetInspection_=false;selectedFile_=doc_.path();
        updateSession();requestFrame();
    }
    void switchDocument(const std::filesystem::path& path,bool) {
        doc_.open(path);resetView();refreshFiles();error_=false;message_="已打开 "+pathText(path.filename());logs_.push_back(message_);
    }
    void stopPreview() {
        edit3DView_.reset();edit3D_.reset();edit3DRevision_=UINT64_MAX;edit3DPath_.clear();
        sceneView_.reset();scenePlay_.reset();scenePaused_=false;sceneGameView_=false;setGameFocus(false);pendingSceneInput_={};scenePressed_=gm::NoIndex;
        if(previewProcess_) {if(WaitForSingleObject(previewProcess_,0)==WAIT_TIMEOUT) TerminateProcess(previewProcess_,0);CloseHandle(previewProcess_);previewProcess_=nullptr;}
    }
    void runPreview() {
        if(sceneMode()){runScenePreview();return;}
        const auto asset=gm::compileUi(doc_.data(),doc_.path().parent_path());
        const auto output=std::filesystem::absolute(session_/"preview.gmui");gm::saveUi(output,asset);
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);auto exe=std::filesystem::path(module).parent_path()/"gm_player.exe";
        std::wstring args=L"\""+exe.wstring()+L"\" --project \""+output.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&startup,&process)) throw std::runtime_error("无法启动 Player，请先构建 gm_player");
        stopPreview();previewProcess_=process.hProcess;CloseHandle(process.hThread);message_="已运行当前界面（包含未保存的修改）";logs_.push_back(message_);
    }
    void useImage(bool apply) {
        if(!isImage(selectedFile_)) throw std::runtime_error("请先在资源面板选择 PNG / JPEG 图片");
        std::error_code ec;auto relative=std::filesystem::relative(selectedFile_,doc_.path().parent_path(),ec);const auto source=pathText(ec?selectedFile_:relative);
        if(sceneMode()){if(apply){command({{"op","component.set"},{"id",doc_.selected()},{"type","SpriteRenderer"},{"field","image"},{"value",source}});}else {auto id=gm::newEntityId();auto result=command({{"op","batch"},{"commands",Json::array({{{"op","entity.create"},{"id",id},{"name",pathText(selectedFile_.stem())}},{{"op","component.add"},{"id",id},{"type","SpriteRenderer"}},{{"op","component.set"},{"id",id},{"type","SpriteRenderer"},{"field","image"},{"value",source}}})}});if(result.value("ok",false))command({{"op","select"},{"id",id}});}return;}
        if(apply) {
            const auto* node=selectedNode();if(!node || node->at("type")!="image") throw std::runtime_error("请先选择画布中的图片节点");
            command({{"op","set"},{"id",doc_.selected()},{"property","source"},{"value",source}});
        } else {
            std::string id="image";int i=1;while(gm::findNode(doc_.data()["root"],id)) id="image"+std::to_string(i++);
            auto result=command({{"op","add"},{"parent",rootId()},{"node",{{"id",id},{"name",pathText(selectedFile_.stem())},{"type","image"},{"x",24},{"y",24},{"w",240},{"h",180},{"source",source},{"fit","contain"}}}});
            if(result.value("ok",false)) command({{"op","select"},{"id",id}});
        }
    }
    void openDialog(bool create,bool scene=false) {
        wchar_t buffer[4096]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;
        dialog.lpstrFilter=L"引擎资源 (*.scene.json;*.ui.json)\0*.scene.json;*.ui.json\0JSON 文件 (*.json)\0*.json\0\0";dialog.lpstrFile=buffer;dialog.nMaxFile=4096;
        auto initial=(contentRoot()/(scene?"assets/scenes":"assets/ui")).wstring();dialog.lpstrInitialDir=initial.c_str();dialog.lpstrDefExt=scene?L"scene.json":L"ui.json";
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(create?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(!(create?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))) return;
        std::filesystem::path path(buffer);
        if(create) for(const auto& tab:doc_.tabs()) {std::error_code ec;if(std::filesystem::equivalent(tab->path(),path,ec)) throw std::runtime_error("该资源已在标签中打开，请先关闭对应标签或选择其他文件名。");}
        if(create) {if(scene)gm::writeJsonAtomic(path,gm::newScene());else gm::writeJsonAtomic(path,{{"format","gm.ui"},{"version",1},{"root",{{"id","canvas"},{"name","Canvas"},{"type","panel"},{"x",0},{"y",0},{"w",1280},{"h",720},{"color","#2D3036"},{"children",Json::array()}}}});}
        switchDocument(path,true);
    }
    void chooseImage() {
        if(!commit()) return;
        if(sceneMode()){sceneImageDialog();return;}
        const auto* node=selectedNode();if(!node || node->at("type")!="image") return;
        wchar_t buffer[4096]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;
        dialog.lpstrFilter=L"图片 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0\0";dialog.lpstrFile=buffer;dialog.nMaxFile=4096;
        const auto source=node->value("source",std::string());
        auto current=source.empty()?contentRoot()/"assets/images":doc_.path().parent_path()/std::filesystem::path(gm::wide(source));
        current=std::filesystem::absolute(current).lexically_normal();current.make_preferred();
        const auto initial=(source.empty()?current:current.parent_path()).wstring();dialog.lpstrInitialDir=initial.c_str();
        const auto filename=source.empty()?initial+L"\\":current.wstring();
        if(filename.size()<4096) std::copy(filename.begin(),filename.end(),buffer);
        dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&dialog)) return;
        std::error_code ec;auto relative=std::filesystem::relative(buffer,doc_.path().parent_path(),ec);
        auto path=ec?std::filesystem::path(buffer):relative;const auto utf8=path.generic_u8string();
        command({{"op","set"},{"id",doc_.selected()},{"property","source"},{"value",std::string(utf8.begin(),utf8.end())}});images_->clear();
    }
    void chooseColor(const std::string& property) {
        if(!commit()) return;
        auto* node=selectedNode();
        auto value=node->value(property,defaultValue(*node,property)).get<std::string>();
        static COLORREF custom[16]{};CHOOSECOLORW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=window_;picker.rgbResult=gm::color(value)&0xffffff;picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&picker)) { char hex[8];snprintf(hex,sizeof(hex),"#%02X%02X%02X",GetRValue(picker.rgbResult),GetGValue(picker.rgbResult),GetBValue(picker.rgbResult));command({{"op","set"},{"id",doc_.selected()},{"property",property},{"value",hex}}); }
    }
    void showMenu(const std::string& name) {
        if(name=="project"){projectMenu();return;}
        if(sceneMode()){sceneMenu(name);return;}
        HMENU menu=CreatePopupMenu();std::vector<Json> actions;
        auto item=[&](const wchar_t* label,Json cmd,bool enabled=true){enabled=enabled&&(!projectBusy()||allowedDuringProjectTask(cmd.value("op",std::string())));actions.push_back(std::move(cmd));AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),UINT_PTR(actions.size()),label);};
        auto separator=[&](){AppendMenuW(menu,MF_SEPARATOR,0,nullptr);};
        const auto current=state();
        if(name=="file") {
            item(L"新建 UI 资源...\tCtrl+N",{{"op","dialog.new"}});item(L"打开...\tCtrl+O",{{"op","dialog.open"}});separator();
            item(L"保存\tCtrl+S",{{"op","save"}});item(L"关闭当前标签\tCtrl+W",{{"op","tab.close"},{"interactive",true}},doc_.tabs().size()>1);item(L"从磁盘重新加载",{{"op","reload"}},!doc_.dirty());
        } else if(name=="edit") {
            item(L"撤销\tCtrl+Z",{{"op","undo"}},current["undo"].get<int>()>0);item(L"重做\tCtrl+Y",{{"op","redo"}},current["redo"].get<int>()>0);separator();
            item(L"复制节点\tCtrl+D",{{"op","duplicate"},{"id",doc_.selected()}},!assetInspection_ && doc_.selected()!=rootId());
            item(L"删除节点\tDelete",{{"op","remove"},{"id",doc_.selected()}},!assetInspection_ && doc_.selected()!=rootId());
        } else if(name=="view") {
            item(L"适应画布\tF",{{"op","view.fit"}});item(grid_?L"隐藏网格":L"显示网格",{{"op","view.grid"}});
            item(L"资源浏览器",{{"op","bottom.tab"},{"name","assets"}});item(L"控制台",{{"op","bottom.tab"},{"name","output"}});separator();item(L"重置面板布局",{{"op","layout.reset"}});
        } else if(name=="tools") {
            item(L"运行当前界面\tF5",{{"op","preview.run"}},rootId()!="editor");separator();
            item(L"编辑编辑器界面...",{{"op","open-editor"}});item(L"应用当前编辑器界面\tCtrl+Shift+F5",{{"op","apply"}},rootId()=="editor");separator();item(L"恢复默认编辑器界面\tF6",{{"op","recover"}});
        } else if(name=="create" || name=="node") {
            const auto* selected=selectedNode();auto parent=selected && selected->at("type")=="panel"?doc_.selected():rootId();
            item(L"面板",{{"op","create"},{"type","panel"},{"parent",parent}});item(L"文本",{{"op","create"},{"type","label"},{"parent",parent}});item(L"按钮",{{"op","create"},{"type","button"},{"parent",parent}});item(L"滚动列表",{{"op","create"},{"type","list"},{"parent",parent}});
            item(L"图片",{{"op","create"},{"type","image"},{"parent",parent}});
            if(name=="node") {separator();item(L"复制节点\tCtrl+D",{{"op","duplicate"},{"id",doc_.selected()}},!assetInspection_ && doc_.selected()!=rootId());item(L"删除节点\tDelete",{{"op","remove"},{"id",doc_.selected()}},!assetInspection_ && doc_.selected()!=rootId());}
        } else if(name=="asset") {
            const auto* node=selectedNode();
            if(isImage(selectedFile_)) {item(L"添加到画布",{{"op","asset.add"}});item(L"用于选中图片",{{"op","asset.apply"}},node && node->at("type")=="image");}
            else item(L"打开资源",{{"op","file.open"},{"path",pathText(selectedFile_)}});
            separator();item(L"刷新资源",{{"op","files.refresh"}});
        } else if(name=="image-fit") {
            item(L"完整显示 (contain)",{{"op","set"},{"id",doc_.selected()},{"property","fit"},{"value","contain"}});
            item(L"填满裁切 (cover)",{{"op","set"},{"id",doc_.selected()},{"property","fit"},{"value","cover"}});
            item(L"拉伸 (stretch)",{{"op","set"},{"id",doc_.selected()},{"property","fit"},{"value","stretch"}});
        }
        POINT point{LONG(mouseX_),LONG(mouseY_+12*scale_)};ClientToScreen(window_,&point);
        auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,point.x,point.y,0,window_,nullptr);DestroyMenu(menu);
        if(choice>0 && choice<=actions.size()) command(actions[choice-1]);
    }
    Json defaultValue(const Json& node,const std::string& key) const {
        if(key=="source") return "";if(key=="fit") return "contain";if(key=="tint") return "#FFFFFF";if(key=="opacity") return 1;
        if(key=="name") return nameOf(node);
        if(key=="color") return "#34373D";if(key=="textColor") return "#DFE7F2";
        if(key=="fontSize") return 14;if(key=="visible") return true;if(key=="text"||key=="bindText"||key=="event") return "";if(key=="layout")return "absolute";if(key=="columns")return 1;if(key=="rowHeight")return 28;if(key=="fillX"||key=="fillY")return false;return 0;
    }
    void startEdit(const std::string& key) {
        if(projectBusy()&&!key.starts_with("$"))return;
        std::string initial;
        if(key=="$tree") initial=treeSearch_;
        else if(key=="$files") initial=fileSearch_;
        else if(sceneMode()){if(scenePlay_)return;auto* e=selectedNode();if(!e)return;if(key=="name")initial=nameOf(*e);else {auto dot=key.rfind('.');auto* c=gm::findComponent(*e,key.substr(0,dot));if(!c||dot==std::string::npos)return;initial=displayValue(c->at("fields").at(key.substr(dot+1)));}}
        else {
            const auto* node=selectedNode();if(!node) return;
            auto value=node->value(key,defaultValue(*node,key));initial=value.is_string()?value.get<std::string>():value.dump();
        }
        auto text=gm::wide(initial);edit_=Edit{doc_.selected(),key,text,text,text.size(),0,doc_.revision()};
    }
    void updateSearch() {
        if(!edit_) return;
        if(edit_->property=="$tree") {treeSearch_=gm::utf8(edit_->buffer);treeScroll_=0;}
        if(edit_->property=="$files") {fileSearch_=gm::utf8(edit_->buffer);fileScroll_=0;}
    }
    bool commit() {
        if(!edit_) return true;
        if(edit_->property.front()=='$' || edit_->buffer==edit_->original) {edit_.reset();return true;}
        const auto edit=*edit_;const auto text=gm::utf8(edit.buffer);Json value;
        try {
            if(sceneMode()&&edit.property.rfind('.')!=std::string::npos){auto dot=edit.property.rfind('.');const auto& def=gm::sceneComponent(edit.property.substr(0,dot));bool string=false;for(const auto& f:def.fields)if(f.key==edit.property.substr(dot+1))string=f.initial.is_string();value=string?Json(text):Json::parse(text);}
            else if(edit.property=="name" || edit.property=="text" || edit.property=="color" || edit.property=="textColor" || edit.property=="source" || edit.property=="tint" || edit.property=="layout" || edit.property=="bindText" || edit.property=="event") value=text;
            else value=Json::parse(text);
        } catch(...) {fail("请输入有效的数值。");return false;}
        auto response=command({{"op","set"},{"id",edit.node},{"property",edit.property},{"value",value},{"expected_revision",edit.revision}});
        if(response.value("ok",false)) {edit_.reset();return true;}return false;
    }
    Rect px(Rect r) const {return {r.x*scale_,r.y*scale_,r.w*scale_,r.h*scale_};}
    uint32_t ink() const {return C("#D3D6DC");} uint32_t muted() const {return C("#9298A2");} uint32_t blue() const {return C("#80ABEB");}
    void text(float x,float y,const std::string& value,float size=13,uint32_t color=0,float limit=10000) {
        renderer_->text(x,y,value,size*scale_,color?color:ink(),limit);
    }
    void centered(Rect r,const std::string& label,float size=13,uint32_t color=0) {
        const float w=renderer_->textWidth(label,size*scale_);
        text(r.x+std::max(4*scale_,(r.w-w)*.5f),r.y+(r.h-size*scale_)*.5f-2*scale_,label,size,color,r.w-8*scale_);
    }
    void button(Rect r,const std::string& label,Json cmd,const std::string& tooltip="",bool active=false,bool enabled=true) {
        enabled=enabled&&(!projectBusy()||allowedDuringProjectTask(cmd.value("op",std::string())));
        if(active || (enabled && r.contains(mouseX_,mouseY_))) renderer_->rect(r,active?C("#344963"):C("#373A40"));
        centered(r,label,13,enabled?(active?C("#DDEBFF"):ink()):C("#646A73"));
        hits_.push_back({r,std::move(cmd),tooltip,enabled});
    }
    void titleBar(Rect r,const std::string& title) {
        renderer_->rect({r.x,r.y,r.w,29*scale_},C("#2B2D32"));
        renderer_->rect({r.x,r.y+28*scale_,r.w,scale_},C("#191B1E"));
        text(r.x+12*scale_,r.y+6*scale_,title,13,ink(),r.w-60*scale_);
    }
    void drawDocument(const Json& root,float x,float y,float scale,bool preview,Rect clip) {
        gm::UiHooks hooks;
        hooks.observe=[&](const Json& node,Rect r) {
            if(preview) nodes_.push_back({r,node["id"].get<std::string>()});
            else if(node["type"]=="button" && node.contains("action")) hits_.push_back({r,node["action"],"",true});
        };
        hooks.paintCustom=[&](const Json& node,Rect r,float unit) {
            const auto kind=node["type"].get<std::string>();
            if(kind=="image") {images_->draw(node,r,unit,preview?doc_.path().parent_path():shellDirectory_);return;}
            if(preview) {
                const auto label=node.value("text",kindName(kind));
                renderer_->border(r,C("#41454C"));
                renderer_->text(r.x+8*unit,r.y+6*unit,label,13*unit,C("#BCC3CE"),r.w-16*unit);
                return;
            }
            if(kind=="menubar") drawMenu(r);
            else if(kind=="toolbar") drawMainToolbar(r);
            else if(kind=="tree") drawTree(r,node.value("text",std::string("场景")));
            else if(kind=="files") drawFiles(r,node.value("text",std::string("文件系统")));
            else if(kind=="viewport") drawViewport(r);
            else if(kind=="inspector") drawInspector(r,node.value("text",std::string("检查器")));
            else if(kind=="output" || kind=="history") drawOutput(r);
            else if(kind=="status") drawStatus(r);
        };
        gm::UiRuntime(*renderer_).render(root,x,y,scale,clip,hooks);
    }
    void drawMenu(Rect r) {
        float x=r.x+46*scale_;
        for(const auto& [label,name]:std::vector<std::pair<std::string,std::string>>{{"文件","file"},{"编辑","edit"},{"节点","node"},{"视图","view"},{"项目","project"},{"工具","tools"}}) {
            button({x,r.y,48*scale_,r.h},sceneMode()&&name=="node"?"实体":label,{{"op","menu"},{"name",name}});x+=50*scale_;
        }
        text(r.x+r.w-124*scale_,r.y+8*scale_,"GM ENGINE  0.8",11,muted(),116*scale_);
    }
    void inputBox(Rect box,const std::string& key,const std::string& value,const std::string& placeholder="",bool property=true) {
        const bool active=edit_ && edit_->property==key && (!property || edit_->node==doc_.selected());
        renderer_->rect(box,C("#1D1F23"));renderer_->border(box,active?C("#6B95D2"):C("#3A3D43"));
        std::string visible=active?gm::utf8(edit_->buffer):value;
        float offset=0;
        if(active) {
            const float caret=renderer_->textWidth(gm::utf8(edit_->buffer.substr(0,edit_->cursor)),13*scale_);
            offset=std::max(0.f,caret-(box.w-18*scale_));
            const auto a=std::min(edit_->cursor,edit_->anchor),b=std::max(edit_->cursor,edit_->anchor);
            const auto left=renderer_->textWidth(gm::utf8(edit_->buffer.substr(0,a)),13*scale_),right=renderer_->textWidth(gm::utf8(edit_->buffer.substr(0,b)),13*scale_);
            float sx=box.x+7*scale_+std::max(0.f,left-offset),ex=std::min(box.x+box.w-6*scale_,box.x+7*scale_+right-offset);
            if(ex>sx) renderer_->rect({sx,box.y+3*scale_,ex-sx,box.h-6*scale_},C("#3E587B"));
            renderer_->rect({box.x+7*scale_+caret-offset,box.y+5*scale_,scale_,box.h-10*scale_},ink());
        }
        bool empty=visible.empty() && !active;
        // Render only the visible suffix; atlas sampling still clips the right edge.
        if(active && offset>0) {size_t i=0;while(i<edit_->buffer.size() && renderer_->textWidth(gm::utf8(edit_->buffer.substr(0,i)),13*scale_)<offset) ++i;visible=gm::utf8(edit_->buffer.substr(i));}
        text(box.x+7*scale_,box.y+4*scale_,empty?placeholder:visible,13,empty?C("#797F88"):ink(),box.w-12*scale_);

        fields_.push_back({box,key});
    }
    void treeRows(const Json& node,int depth,std::vector<std::pair<const Json*,int>>& rows) {
        const auto id=node["id"].get<std::string>();
        if(treeSearch_.empty() || matches(nameOf(node),treeSearch_) || matches(id,treeSearch_)) rows.emplace_back(&node,depth);
        if(node.contains("children") && (treeSearch_.size() || !collapsed_.count(id))) for(const auto& child:node["children"]) treeRows(child,depth+1,rows);
    }
    void scrollbar(Rect r,int count,int visible,int offset) {
        if(count<=visible || r.h<=0) return;
        const float h=std::max(20*scale_,r.h*visible/count);const float y=r.y+(r.h-h)*offset/(count-visible);
        renderer_->rect({r.x+r.w-5*scale_,y,3*scale_,h},C("#555A63"));
    }
    void drawTree(Rect r,const std::string& title) {
        if(sceneMode()){drawSceneTree(r);return;}
        titleBar(r,title);
        button({r.x+r.w-30*scale_,r.y+2*scale_,26*scale_,24*scale_},"+",{{"op","menu"},{"name","create"}},"添加节点");
        inputBox({r.x+8*scale_,r.y+36*scale_,r.w-16*scale_,25*scale_},"$tree",treeSearch_,"筛选节点…",false);
        std::vector<std::pair<const Json*,int>> rows;treeRows(doc_.data()["root"],0,rows);
        const auto list=Rect{r.x,r.y+69*scale_,r.w,r.h-73*scale_};int visible=std::max(1,int(list.h/(26*scale_)));
        treeScroll_=std::clamp(treeScroll_,0,std::max(0,int(rows.size())-visible));
        renderer_->clip(list);
        for(int i=0;i<visible && i+treeScroll_<int(rows.size());++i) {
            const auto [node,depth]=rows[i+treeScroll_];const auto id=node->at("id").get<std::string>();Rect row{list.x,list.y+i*26*scale_,list.w,26*scale_};
            const bool selected=id==doc_.selected();if(selected) renderer_->rect(row,C("#354E70"));else if(row.contains(mouseX_,mouseY_)) renderer_->rect(row,C("#30343B"));
            hits_.push_back({row,{{"op","select"},{"id",id}},"",true});
            const float indent=10*scale_+depth*16*scale_;
            if(node->contains("children") && !node->at("children").empty()) {
                button({row.x+indent,row.y,20*scale_,row.h},collapsed_.count(id)?"+":"−",{{"op","tree.toggle"},{"id",id}},"折叠 / 展开");
            }
            auto kind=node->at("type").get<std::string>();uint32_t color=kind=="button"?C("#9FB5DD"):kind=="label"?C("#B4CC9B"):C("#91B8BA");
            renderer_->border({row.x+indent+21*scale_,row.y+8*scale_,10*scale_,10*scale_},color);
            text(row.x+indent+39*scale_,row.y+5*scale_,nameOf(*node),13,node->value("visible",true)?ink():muted(),row.w-indent-46*scale_);
        }
        scrollbar(list,int(rows.size()),visible,treeScroll_);renderer_->clip(r);
    }
    void scan(const std::filesystem::path& path,int depth) {
        std::error_code ec;if(!std::filesystem::is_directory(path,ec)) return;
        files_.push_back({path,pathText(path.filename()),depth,true});
        std::vector<std::filesystem::directory_entry> entries;
        for(const auto& entry:std::filesystem::directory_iterator(path,std::filesystem::directory_options::skip_permission_denied,ec)) entries.push_back(entry);
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){if(a.is_directory()!=b.is_directory()) return a.is_directory();return a.path()<b.path();});
        for(const auto& entry:entries) {
            if(entry.is_symlink()) continue;
            if(entry.is_directory() && depth<5) scan(entry.path(),depth+1);
            else if(entry.is_regular_file() && (pathText(entry.path().filename()).ends_with(".ui.json") || pathText(entry.path().filename()).ends_with(".scene.json") || isImage(entry.path()))) files_.push_back({entry.path(),pathText(entry.path().filename()),depth+1,false});
        }
    }
    static bool isImage(const std::filesystem::path& path) {
        auto ext=pathText(path.extension());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});return ext==".png" || ext==".jpg" || ext==".jpeg";
    }
    void refreshFiles() {files_.clear();scan(contentRoot()/"assets",0);scan(root_/"editor",0);if(selectedFolder_.empty()) selectedFolder_=contentRoot()/"assets";}
    void drawFiles(Rect r,const std::string&) {
        titleBar(r,"");
        button({r.x+5*scale_,r.y+2*scale_,60*scale_,25*scale_},"资源",{{"op","bottom.tab"},{"name","assets"}},"项目中的 UI 与图片",!layout_.outputOpen);
        button({r.x+69*scale_,r.y+2*scale_,60*scale_,25*scale_},"控制台",{{"op","bottom.tab"},{"name","output"}},"编译与编辑消息",layout_.outputOpen);
        if(layout_.outputOpen) {
            button({r.x+r.w-60*scale_,r.y+2*scale_,52*scale_,24*scale_},"清空",{{"op","output.clear"}});
            int visible=std::max(0,int((r.h-40*scale_)/(24*scale_))),begin=std::max(0,int(logs_.size())-visible);
            for(int i=begin;i<int(logs_.size());++i) text(r.x+13*scale_,r.y+(38+(i-begin)*24)*scale_,logs_[i],12,muted(),r.w-26*scale_);
            return;
        }
        button({r.x+r.w-60*scale_,r.y+2*scale_,52*scale_,24*scale_},"刷新",{{"op","files.refresh"}},"重新读取图片与资源列表");
        const float side=std::min(180*scale_,r.w*.26f);
        Rect folders{r.x,r.y+30*scale_,side,r.h-30*scale_};renderer_->rect(folders,C("#222429"));renderer_->clip(folders);
        float y=folders.y+6*scale_;
        const int folderCount=int(std::count_if(files_.begin(),files_.end(),[](const auto& file){return file.folder;}));
        const int folderRows=std::max(1,int((folders.h-6*scale_)/(25*scale_)));folderScroll_=std::clamp(folderScroll_,0,std::max(0,folderCount-folderRows));int folderIndex=0;
        for(const auto& file:files_) if(file.folder) {
            if(folderIndex++<folderScroll_) continue;
            Rect row{folders.x,y,folders.w,25*scale_};if(row.y+row.h>r.y+r.h) break;
            if(file.path==selectedFolder_) renderer_->rect(row,C("#35445A"));
            float indent=(12+file.depth*13)*scale_;renderer_->rect({row.x+indent,row.y+9*scale_,11*scale_,8*scale_},C("#B3A680"));
            text(row.x+indent+18*scale_,row.y+4*scale_,file.label,12,ink(),row.w-indent-22*scale_);
            hits_.push_back({row,{{"op","files.folder"},{"path",pathText(file.path)}},pathText(file.path),true});y+=25*scale_;
        }
        scrollbar(folders,folderCount,folderRows,folderScroll_);renderer_->clip(r);const float start=r.x+side+12*scale_;
        std::error_code ec;auto relative=std::filesystem::relative(selectedFolder_,contentRoot(),ec);
        text(start,r.y+38*scale_,pathText(ec?selectedFolder_:relative),12,muted(),std::max(30*scale_,r.w-side-224*scale_));
        inputBox({r.x+r.w-206*scale_,r.y+33*scale_,194*scale_,25*scale_},"$files",fileSearch_,"搜索资源…",false);
        std::vector<const FileItem*> filtered;
        for(const auto& file:files_) if(!file.folder && (fileSearch_.empty()?file.path.parent_path()==selectedFolder_:matches(file.label,fileSearch_))) filtered.push_back(&file);
        Rect list{start,r.y+66*scale_,r.w-side-20*scale_,r.h-70*scale_};renderer_->clip(list);
        const int columns=std::max(1,int(list.w/(130*scale_))),rows=std::max(1,int(list.h/(110*scale_)));
        fileScroll_=std::clamp(fileScroll_,0,std::max(0,(int(filtered.size())+columns-1)/columns-rows));
        for(int n=0;n<columns*rows && n+fileScroll_*columns<int(filtered.size());++n) {
            const auto* file=filtered[n+fileScroll_*columns];Rect tile{list.x+(n%columns)*130*scale_,list.y+(n/columns)*110*scale_,122*scale_,104*scale_};
            if(selectedFile_==file->path) renderer_->rect(tile,C("#34465D"));else if(tile.contains(mouseX_,mouseY_)) renderer_->rect(tile,C("#30343B"));
            Rect thumb{tile.x+12*scale_,tile.y+5*scale_,98*scale_,66*scale_};renderer_->rect(thumb,C("#1B1E23"));
            if(isImage(file->path)) images_->draw({{"source",pathText(file->path)},{"fit","contain"}},thumb,scale_,root_);
            else {renderer_->border({thumb.x+32*scale_,thumb.y+8*scale_,34*scale_,47*scale_},C("#8CA6CC"));centered(thumb,file->label.ends_with(".scene.json")?"SCENE":"UI",13,C("#B0C5E1"));}
            text(tile.x+5*scale_,tile.y+76*scale_,file->label,12,ink(),tile.w-10*scale_);
            hits_.push_back({tile,{{"op","files.select"},{"path",pathText(file->path)}},isImage(file->path)?"选择预览 · 右键使用图片":"双击打开 "+file->label,true});
        }
        if(filtered.empty()) text(list.x+12*scale_,list.y+12*scale_,fileSearch_.empty()?"此文件夹没有场景、UI 或图片资源":"没有匹配的资源",12,muted(),list.w-24*scale_);
        scrollbar(list,(int(filtered.size())+columns-1)/columns,rows,fileScroll_);renderer_->clip(r);
    }
    void zoomAt(float x,float y,float factor) {
        if(!std::isfinite(factor) || factor<=0) throw std::runtime_error("Zoom factor must be positive");
        if(canvasArea_.w<=0 || previewScale_<=0) return;
        float next=std::clamp(zoom_*factor,.15f,8.f),ratio=next/zoom_;
        float rx=(x-previewRect_.x)/previewScale_,ry=(y-previewRect_.y)/previewScale_;
        const float newScale=previewScale_*ratio,rw=sceneMode()?doc_.data()["width"].get<float>():doc_.data()["root"]["w"].get<float>(),rh=sceneMode()?doc_.data()["height"].get<float>():doc_.data()["root"]["h"].get<float>();
        panX_=x-rx*newScale-(canvasArea_.x+canvasArea_.w*.5f-rw*newScale*.5f);
        panY_=y-ry*newScale-(canvasArea_.y+canvasArea_.h*.5f-rh*newScale*.5f);
        zoom_=next;
    }
    void drawViewport(Rect r) {
        renderer_->rect({r.x,r.y,r.w,30*scale_},C("#24262A"));
        const size_t capacity=std::max<size_t>(1,size_t((r.w-54*scale_)/(160*scale_)));
        const size_t first=doc_.active()>=capacity?doc_.active()-capacity+1:0;
        const float tabW=std::min(190*scale_,(r.w-54*scale_)/float(std::min(capacity,doc_.tabs().size())));
        for(size_t i=first;i<doc_.tabs().size() && i<first+capacity;++i) {
            Rect tab{r.x+float(i-first)*tabW,r.y,tabW,30*scale_};
            const auto& doc=*doc_.tabs()[i];const bool active=i==doc_.active();
            if(active) {renderer_->rect(tab,C("#30333A"));renderer_->rect({tab.x,tab.y,tab.w,2*scale_},C("#799BC9"));}
            text(tab.x+10*scale_,tab.y+7*scale_,pathText(doc.path().filename())+(doc.dirty()?" *":""),12,active?ink():muted(),tab.w-36*scale_);
            hits_.push_back({tab,{{"op","tab.select"},{"index",i}},pathText(doc.path()),true});
            if(active && doc_.tabs().size()>1) button({tab.x+tab.w-25*scale_,tab.y+3*scale_,22*scale_,24*scale_},"×",{{"op","tab.close"},{"interactive",true}},"关闭标签  Ctrl+W");
        }
        if(doc_.tabs().size()>capacity) button({r.x+r.w-51*scale_,r.y+3*scale_,48*scale_,24*scale_},"切换",{{"op","tab.select"},{"index",(doc_.active()+1)%doc_.tabs().size()}},"下一个标签  Ctrl+Tab");

        if(sceneMode()){drawSceneViewport(r);return;}
        const float y=r.y+33*scale_,h=26*scale_;
        button({r.x+8*scale_,y,51*scale_,h},"选择",{{"op","view.tool"},{"tool","select"}},"选择节点  Q",tool_=="select");
        button({r.x+62*scale_,y,51*scale_,h},"移动",{{"op","view.tool"},{"tool","move"}},"移动节点  W",tool_=="move");
        button({r.x+114*scale_,y,40*scale_,h},"网格",{{"op","view.grid"}},"显示 / 隐藏网格",grid_);
        float zx=r.x+r.w-158*scale_;
        button({zx,y,26*scale_,h},"−",{{"op","view.zoom"},{"factor",1/1.2f}},"缩小");
        const float rw=doc_.data()["root"]["w"].get<float>(),rh=doc_.data()["root"]["h"].get<float>();
        canvasArea_={r.x+20*scale_,r.y+82*scale_,r.w-20*scale_,r.h-104*scale_};
        const float fit=std::max(.01f,std::min((canvasArea_.w-90*scale_)/rw,(canvasArea_.h-90*scale_)/rh));
        previewScale_=fit*zoom_;previewRect_={canvasArea_.x+(canvasArea_.w-rw*previewScale_)*.5f+panX_,canvasArea_.y+(canvasArea_.h-rh*previewScale_)*.5f+panY_,rw*previewScale_,rh*previewScale_};
        centered({zx+24*scale_,y,48*scale_,h},std::to_string(int(previewScale_/scale_*100))+"%",12);
        button({zx+73*scale_,y,26*scale_,h},"+",{{"op","view.zoom"},{"factor",1.2f}},"放大");
        button({zx+101*scale_,y,53*scale_,h},"适应",{{"op","view.fit"}},"适应画布  F");
        renderer_->rect({r.x,r.y+63*scale_,r.w,scale_},C("#111316"));
        renderer_->clip(canvasArea_);
        if(grid_) {
            float step=32*previewScale_;while(step<14*scale_) step*=2;
            for(float x=canvasArea_.x+std::fmod(previewRect_.x-canvasArea_.x,step);x<canvasArea_.x+canvasArea_.w;x+=step) renderer_->rect({x,canvasArea_.y,1,canvasArea_.h},C("#292C31"));
            for(float gy=canvasArea_.y+std::fmod(previewRect_.y-canvasArea_.y,step);gy<canvasArea_.y+canvasArea_.h;gy+=step) renderer_->rect({canvasArea_.x,gy,canvasArea_.w,1},C("#292C31"));
        }
        auto scene=doc_.data()["root"];scene["x"]=0;scene["y"]=0;
        if(dragMode_=="node" || dragMode_=="resize") if(auto* n=gm::findNode(scene,dragId_)) {
            if(dragMode_=="node") {(*n)["x"]=nodeX_+dx_;(*n)["y"]=nodeY_+dy_;}
            else {(*n)["w"]=std::max(1.f,nodeW_+dx_);(*n)["h"]=std::max(1.f,nodeH_+dy_);}
        }
        drawDocument(scene,previewRect_.x,previewRect_.y,previewScale_,true,canvasArea_);
        renderer_->clip(canvasArea_);renderer_->border(previewRect_,C("#737A85"),1);
        for(const auto& node:nodes_) if(node.id==doc_.selected()) {
            renderer_->border(node.rect,blue(),1);
            if(node.id!=rootId()) for(auto [x,sy]:{std::pair{node.rect.x,node.rect.y},std::pair{node.rect.x+node.rect.w,node.rect.y},std::pair{node.rect.x,node.rect.y+node.rect.h},std::pair{node.rect.x+node.rect.w,node.rect.y+node.rect.h}}) {
                renderer_->rect({x-3*scale_,sy-3*scale_,6*scale_,6*scale_},C("#CADBF5"));renderer_->border({x-3*scale_,sy-3*scale_,6*scale_,6*scale_},C("#608BC3"));
            }
        }
        renderer_->clip(r);renderer_->rect({r.x+20*scale_,r.y+64*scale_,r.w-20*scale_,18*scale_},C("#24262A"));
        renderer_->rect({r.x,r.y+64*scale_,20*scale_,r.h-86*scale_},C("#24262A"));
        const float tick=100*previewScale_;
        if(tick>22*scale_) for(int i=0;i<=int(rw/100);++i) {
            float x=previewRect_.x+i*tick;if(x>r.x+22*scale_ && x<r.x+r.w-30*scale_) {
                renderer_->rect({x,r.y+76*scale_,1,6*scale_},C("#626873"));text(x+3*scale_,r.y+64*scale_,std::to_string(i*100),10,muted(),35*scale_);
            }
        }
        if(tick>22*scale_) for(int i=0;i<=int(rh/100);++i) {
            float sy=previewRect_.y+i*tick;if(sy>r.y+84*scale_ && sy<r.y+r.h-25*scale_) renderer_->rect({r.x+14*scale_,sy,6*scale_,1},C("#626873"));
        }
        renderer_->rect({r.x,r.y+r.h-22*scale_,r.w,22*scale_},C("#24262A"));
        text(r.x+12*scale_,r.y+r.h-18*scale_,std::to_string(int(rw))+" × "+std::to_string(int(rh)),11,muted());
        const auto* selected=selectedNode();
        if(selected) text(r.x+r.w-230*scale_,r.y+r.h-18*scale_,nameOf(*selected)+"   "+std::to_string(int((*selected)["w"].get<float>()))+" × "+std::to_string(int((*selected)["h"].get<float>())),11,muted(),218*scale_);
    }
    std::string displayValue(const Json& value) {
        if(value.is_string()) return value.get<std::string>();
        if(value.is_number()) { std::ostringstream s;s<<std::setprecision(7)<<value.get<double>();return s.str(); }
        return value.dump();
    }
    void property(Rect r,const Json& node,const std::string& key,const std::string& label,float labelWidth=76) {
        text(r.x,r.y+5*scale_,label,12,muted(),labelWidth*scale_-6*scale_);
        inputBox({r.x+labelWidth*scale_,r.y,r.w-labelWidth*scale_,25*scale_},key,displayValue(node.value(key,defaultValue(node,key))));
    }
    bool group(Rect r,const std::string& name) {
        renderer_->rect(r,C("#2D3035"));text(r.x+8*scale_,r.y+5*scale_,(groups_.count(name)?"▸  ":"▾  ")+name,12,ink(),r.w-16*scale_);
        hits_.push_back({r,{{"op","group.toggle"},{"name",name}},"",true});return !groups_.count(name);
    }
    void colorProperty(Rect r,const Json& node,const std::string& key,const std::string& label) {
        text(r.x,r.y+5*scale_,label,12,muted(),72*scale_);
        const auto value=node.value(key,defaultValue(node,key)).get<std::string>();Rect swatch{r.x+76*scale_,r.y+2*scale_,27*scale_,21*scale_};
        renderer_->rect(swatch,gm::color(value));renderer_->border(swatch,C("#606773"));
        hits_.push_back({swatch,{{"op","dialog.color"},{"property",key}},"选择颜色",true});
        inputBox({r.x+109*scale_,r.y,r.w-109*scale_,25*scale_},key,value);
    }
    void drawAssetInspector(Rect r) {
        titleBar(r,"资源检查器");Rect body{r.x+14*scale_,r.y+42*scale_,r.w-28*scale_,r.h-54*scale_};
        text(body.x,body.y,pathText(selectedFile_.filename()),15,ink(),body.w);
        if(isImage(selectedFile_)) {
            Rect preview{body.x,body.y+34*scale_,body.w,160*scale_};renderer_->rect(preview,C("#1A1D22"));
            images_->draw({{"source",pathText(selectedFile_)},{"fit","contain"}},preview,scale_,root_);
            text(body.x,body.y+208*scale_,images_->description(selectedFile_),12,muted(),body.w);
            button({body.x,body.y+247*scale_,body.w,30*scale_},"+ 添加到画布",{{"op","asset.add"}},"创建图片节点并引用此素材",true);
            auto* node=selectedNode();
            button({body.x,body.y+286*scale_,body.w,30*scale_},"用于选中对象",{{"op","asset.apply"}},"替换选中对象的图片",false,node && (sceneMode()?gm::findComponent(*node,"SpriteRenderer")!=nullptr:node->at("type")=="image"));
        } else {
            text(body.x,body.y+37*scale_,(pathText(selectedFile_).ends_with(".scene.json")?"场景资源":"UI 界面资源"),12,muted(),body.w);
            button({body.x,body.y+76*scale_,body.w,30*scale_},"打开资源",{{"op","file.open"},{"path",pathText(selectedFile_)}},"在文档标签中打开",true);
        }
    }
    void drawInspector(Rect r,const std::string& title) {
        if(inspectorPage_=="project") {inspectorTabs(r);drawProjectInspector(r);return;}
        if(inspectorPage_=="profiler") {inspectorTabs(r);drawProfiler(r);return;}
        if(assetInspection_) {drawAssetInspector(r);return;}
        if(sceneMode()){drawSceneInspector(r);return;}

        titleBar(r,title);const auto* node=selectedNode();if(!node) return;
        const auto firstField=fields_.size(),firstHit=hits_.size();
        const auto type=node->at("type").get<std::string>();
        Rect body{r.x+12*scale_,r.y+40*scale_,r.w-24*scale_,r.h-48*scale_};
        inspectorScroll_=std::clamp(inspectorScroll_,0.f,std::max(0.f,520.f-body.h/scale_));renderer_->clip(body);
        float y=body.y-inspectorScroll_*scale_;
        renderer_->border({body.x,y+3*scale_,17*scale_,17*scale_},C("#9BB5D7"));
        text(body.x+28*scale_,y,kindName(type),16,ink(),body.w-34*scale_);y+=37*scale_;
        property({body.x,y,body.w,25*scale_},*node,"name","名称");y+=36*scale_;
        text(body.x,y+3*scale_,"可见",12,muted(),65*scale_);Rect check{body.x+76*scale_,y+2*scale_,16*scale_,16*scale_};
        renderer_->rect(check,node->value("visible",true)?C("#537AAE"):C("#1D1F23"));renderer_->border(check,C("#727B88"));
        if(node->value("visible",true)) text(check.x+2*scale_,check.y-2*scale_,"✓",13,C("#E5EEFF"),check.w);
        hits_.push_back({{check.x-3*scale_,check.y-3*scale_,22*scale_,22*scale_},{{"op","set"},{"id",doc_.selected()},{"property","visible"},{"value",!node->value("visible",true)}},"显示 / 隐藏节点",true});y+=34*scale_;
        if(group({body.x,y,body.w,27*scale_},"布局")) {
            y+=37*scale_;float w=(body.w-12*scale_)*.5f;
            property({body.x,y,w,25*scale_},*node,"x","X",25);property({body.x+w+12*scale_,y,w,25*scale_},*node,"y","Y",25);y+=34*scale_;
            property({body.x,y,w,25*scale_},*node,"w","宽",25);property({body.x+w+12*scale_,y,w,25*scale_},*node,"h","高",25);y+=42*scale_;
        } else y+=37*scale_;
        if(type!="label" && type!="image") {
            if(group({body.x,y,body.w,27*scale_},"外观")) {y+=37*scale_;colorProperty({body.x,y,body.w,25*scale_},*node,"color","背景");y+=43*scale_;}
            else y+=37*scale_;
        }
        if(type=="label" || type=="button" || type=="list") {
            if(group({body.x,y,body.w,27*scale_},"文字")) {
                y+=37*scale_;property({body.x,y,body.w,25*scale_},*node,"text","内容");y+=34*scale_;
                property({body.x,y,body.w,25*scale_},*node,"fontSize","字号");y+=34*scale_;
                colorProperty({body.x,y,body.w,25*scale_},*node,"textColor","颜色");y+=40*scale_;
            } else y+=37*scale_;
        }
        if(type=="image") {
            if(group({body.x,y,body.w,27*scale_},"图片")) {
                y+=37*scale_;property({body.x,y,body.w-32*scale_,25*scale_},*node,"source","素材");
                button({body.x+body.w-28*scale_,y,28*scale_,25*scale_},"…",{{"op","dialog.image"}},"选择 PNG / JPEG 图片");y+=34*scale_;
                const auto fit=node->value("fit",std::string("contain"));
                text(body.x,y+5*scale_,"适配",12,muted());
                Rect box{body.x+76*scale_,y,body.w-76*scale_,25*scale_};renderer_->rect(box,C("#1D1F23"));renderer_->border(box,C("#3A3D43"));
                button(box,(fit=="contain"?"完整显示":fit=="cover"?"填满裁切":"拉伸")+std::string("  ▾"),{{"op","menu"},{"name","image-fit"}});y+=34*scale_;
                colorProperty({body.x,y,body.w,25*scale_},*node,"tint","着色");y+=34*scale_;
                property({body.x,y,body.w,25*scale_},*node,"opacity","不透明度");y+=34*scale_;
                text(body.x,y,"0 为透明，1 为不透明",11,muted(),body.w);y+=26*scale_;
            } else y+=37*scale_;
        }
        if(group({body.x,y,body.w,27*scale_},"布局与事件")){
            y+=37*scale_;
            for(const auto &[key,label]:std::initializer_list<std::pair<const char*,const char*>>{{"layout","排列"},{"padding","内边距"},{"gap","间距"},{"anchorX","锚点 X"},{"anchorY","锚点 Y"},{"pivotX","轴心 X"},{"pivotY","轴心 Y"},{"fillX","填充宽"},{"fillY","填充高"},{"columns","网格列"},{"rowHeight","行高"},{"bindText","文字绑定"},{"event","点击事件"}}){property({body.x,y,body.w,25*scale_},*node,key,label);y+=34*scale_;}
            text(body.x,y,"排列：absolute / row / column / grid",11,muted(),body.w);y+=28*scale_;
        }else y+=37*scale_;
        auto clipped=[&](Rect box) {
            float x=std::max(box.x,body.x),y=std::max(box.y,body.y);
            return Rect{x,y,std::max(0.f,std::min(box.x+box.w,body.x+body.w)-x),std::max(0.f,std::min(box.y+box.h,body.y+body.h)-y)};
        };
        for(size_t i=firstField;i<fields_.size();++i) fields_[i].rect=clipped(fields_[i].rect);
        for(size_t i=firstHit;i<hits_.size();++i) hits_[i].rect=clipped(hits_[i].rect);
        renderer_->clip(r);
    }
    void drawOutput(Rect r) {
        renderer_->rect({r.x,r.y,r.w,28*scale_},C("#282B30"));
        button({r.x+5*scale_,r.y+2*scale_,76*scale_,24*scale_},layout_.outputOpen?"▾ 输出":"▸ 输出",{{"op","output.toggle"}},"展开 / 收起输出面板",layout_.outputOpen);
        if(errorCount_) text(r.x+94*scale_,r.y+6*scale_,std::to_string(errorCount_)+" 条错误",12,C("#D69D98"));
        if(!layout_.outputOpen) return;
        button({r.x+r.w-56*scale_,r.y+2*scale_,50*scale_,24*scale_},"清空",{{"op","output.clear"}},"清空输出");
        int visible=std::max(0,int((r.h-40*scale_)/(24*scale_))),begin=std::max(0,int(logs_.size())-visible);
        for(int i=begin;i<int(logs_.size());++i) text(r.x+13*scale_,r.y+(38+(i-begin)*24)*scale_,logs_[i],12,muted(),r.w-26*scale_);
    }
    void drawStatus(Rect r) {
        renderer_->rect({r.x,r.y,r.w,scale_},C("#16181B"));
        auto display=projectBusy()?projectTaskText():message_;
        if(display.empty()) {std::error_code ec;auto relative=std::filesystem::relative(doc_.path(),contentRoot(),ec);display=pathText(ec?doc_.path():relative);}
        text(r.x+10*scale_,r.y+4*scale_,display,11,error_?C("#DDA29A"):muted(),r.w-(projectBusy()?225:145)*scale_);
        if(projectBusy())button({r.x+r.w-210*scale_,r.y,75*scale_,22*scale_},"取消任务",{{"op","project.task.cancel"}},"取消当前后台任务",false,!cancellingProjectTask_);
        text(r.x+r.w-120*scale_,r.y+4*scale_,doc_.dirty()?"● 未保存":"已保存",11,doc_.dirty()?C("#D1B88A"):muted(),110*scale_);
    }
    std::vector<std::pair<std::string,Rect>> splitters() const {
        return {{"left",px(layout_.leftSplit)},{"right",px(layout_.rightSplit)},{"output",px(layout_.outputSplit)}};
    }
    void drawSplitters() {
        for(const auto& [name,r]:splitters()) if(r.contains(mouseX_,mouseY_) || dragMode_==name) renderer_->rect(r,C("#587BA7"));
    }
    void setCursor() {
        LPCWSTR cursor=IDC_ARROW;
        for(const auto& [name,r]:splitters()) if(r.contains(mouseX_,mouseY_) || dragMode_==name) cursor=(name=="left"||name=="right")?IDC_SIZEWE:IDC_SIZENS;
        if(dragMode_=="pan") cursor=IDC_SIZEALL;
        for(const auto& field:fields_) if(field.rect.contains(mouseX_,mouseY_)) cursor=IDC_IBEAM;
        SetCursor(LoadCursor(nullptr,cursor));
    }
    void drawTooltip() {
        if(edit_ || !dragMode_.empty() || std::chrono::steady_clock::now()-hoverTime_<std::chrono::milliseconds(850)) return;
        for(auto it=hits_.rbegin();it!=hits_.rend();++it) if(it->rect.contains(mouseX_,mouseY_) && !it->tooltip.empty()) {
            const float w=std::min(360*scale_,renderer_->textWidth(it->tooltip,12*scale_)+20*scale_);
            Rect r{std::min(mouseX_+12*scale_,width_*scale_-w-8*scale_),std::min(mouseY_+22*scale_,height_*scale_-32*scale_),w,27*scale_};
            renderer_->rect(r,C("#42464E"));renderer_->border(r,C("#686E78"));text(r.x+9*scale_,r.y+5*scale_,it->tooltip,12,ink(),r.w-18*scale_);return;
        }
    }
    HWND window_;gm::EditorDocuments doc_;gm::GameProject gameProject_;std::string inspectorPage_="components";Json shell_;std::filesystem::path session_,root_,selectedFile_,selectedFolder_;HANDLE lock_=INVALID_HANDLE_VALUE,previewProcess_=nullptr;
    std::unique_ptr<gm::Renderer> renderer_;std::unique_ptr<gm::AuthoringImages> images_;std::filesystem::path shellDirectory_;gm::WorkspaceLayout layout_;
    std::unique_ptr<gm::SceneRuntime> edit3D_;std::unique_ptr<gm::SceneView> edit3DView_;uint64_t edit3DRevision_=UINT64_MAX;std::filesystem::path edit3DPath_;
    std::unique_ptr<gm::SceneRuntime> scenePlay_;std::unique_ptr<gm::SceneView> sceneView_;
    bool scenePaused_=false,sceneGameView_=false,gameFocus_=false;uint32_t scenePressed_=gm::NoIndex;
    gm::SceneInput pendingSceneInput_{};
    gm::WindowsGameInput gameInput_;
    std::chrono::steady_clock::time_point lastSceneTick_=std::chrono::steady_clock::now();
    std::vector<Hit> hits_;std::vector<Field> fields_;std::vector<NodeHit> nodes_;std::vector<FileItem> files_;std::vector<std::string> logs_;
    std::optional<Edit> edit_;std::set<std::string> collapsed_,groups_;
    std::string message_,treeSearch_,fileSearch_,dragMode_,dragId_,tool_="select";std::wstring lastTitle_;
    bool error_=false,grid_=true,redraw_=true,tooltipPending_=false,assetInspection_=false;int errorCount_=0,treeScroll_=0,fileScroll_=0,folderScroll_=0;float inspectorScroll_=0;
    float scale_=1,width_=1440,height_=900,mouseX_=0,mouseY_=0,hoverX_=0,hoverY_=0;
    float zoom_=1,panX_=0,panY_=0,oldPanX_=0,oldPanY_=0,previewScale_=1,startX_=0,startY_=0,nodeX_=0,nodeY_=0,nodeW_=0,nodeH_=0,dx_=0,dy_=0;
    uint64_t dragRevision_=0;Rect previewRect_{},canvasArea_{};
    std::chrono::steady_clock::time_point hoverTime_=std::chrono::steady_clock::now();
};
Editor* app=nullptr;
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    try {
        if(message==WM_GETMINMAXINFO) {
            auto* info=reinterpret_cast<MINMAXINFO*>(lp);float s=float(GetDpiForWindow(window))/96.f;
            info->ptMinTrackSize={LONG(960*s),LONG(600*s)};return 0;
        }
        if(app) switch(message) {
            case WM_SIZE:app->requestFrame();break;
            case WM_PAINT: {PAINTSTRUCT paint;BeginPaint(window,&paint);EndPaint(window,&paint);app->requestFrame();return 0;}
            case WM_RBUTTONUP:if(!app->gameRight(false))app->context(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK: SetFocus(window);app->mouseDown(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)),message==WM_LBUTTONDBLCLK);return 0;
            case WM_MBUTTONDOWN: SetFocus(window);app->mouseDown(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)),false,true);return 0;
            case WM_RBUTTONDOWN:app->gameRight(true);return 0;
            case WM_KILLFOCUS:app->gameBlur();break;
            case WM_MOUSEMOVE:app->mouseMove(float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp)));return 0;
            case WM_LBUTTONUP:case WM_MBUTTONUP:app->mouseUp();return 0;
            case WM_CAPTURECHANGED:app->captureLost();return 0;
            case WM_MOUSEWHEEL: {POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);app->wheel(GET_WHEEL_DELTA_WPARAM(wp),float(p.x),float(p.y));return 0;}
            case WM_POINTERDOWN:case WM_POINTERUPDATE:case WM_POINTERUP:if(app->gameTouch(wp,message!=WM_POINTERUP,message==WM_POINTERDOWN))return 0;break;
            case WM_KEYDOWN:app->gameKey(uint32_t(wp),(lp & (1ll<<30))!=0);app->key(wp);return 0;
            case WM_CHAR:app->input(wchar_t(wp));return 0;
            case WM_IME_COMPOSITION:
                if(lp&GCS_RESULTSTR) {
                    HIMC imc=ImmGetContext(window);LONG bytes=ImmGetCompositionStringW(imc,GCS_RESULTSTR,nullptr,0);
                    if(bytes>0) {std::wstring result(size_t(bytes)/sizeof(wchar_t),L'\0');ImmGetCompositionStringW(imc,GCS_RESULTSTR,result.data(),bytes);app->ime(result);}
                    ImmReleaseContext(window,imc);return 0;
                } break;
            case WM_CLOSE:if(app->close()) DestroyWindow(window);return 0;
        }
        if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
        if(message==WM_ERASEBKGND) return 1;
    } catch(const std::exception& e) {MessageBoxW(window,gm::wide(e.what()).c_str(),L"GM Engine",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(window,message,wp,lp);
}
}
#include "project_launcher.hpp"
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {
        SetProcessDPIAware();
        std::filesystem::path project,game,session;
        int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
        for(int i=1;i<argc;++i) {std::wstring arg=args[i];if(arg==L"--project" && i+1<argc) project=args[++i];else if(arg==L"--game"&&i+1<argc)game=args[++i];else if(arg==L"--session" && i+1<argc) session=args[++i];else if(arg==L"--editor-shell")project=gm::engineRoot()/"editor/editor.ui.json";else throw std::runtime_error("Expected --project FILE, --game MANIFEST, --editor-shell or --session DIRECTORY");}
        LocalFree(args);
        gm::GameProject gameProject;
        for(;;){
            if(project.empty())project=game.empty()?gm::chooseProject():game;
            if(project.empty())return 0;
            try{gameProject=gm::activateProject(project,game);break;}
            catch(const std::exception& error){MessageBoxW(nullptr,gm::wide(error.what()).c_str(),L"无法打开项目",MB_OK|MB_ICONERROR);project.clear();game.clear();}
        }
        if(session.empty()){auto text=std::filesystem::absolute(project).generic_u8string();std::string key(text.begin(),text.end());session=gm::userDataRoot()/"sessions"/std::to_string(gm::sdk::id(key.c_str()))/"live";}
        gm::rememberProject(gameProject);
        WNDCLASSW cls{};cls.style=CS_DBLCLKS;cls.lpfnWndProc=windowProc;cls.hInstance=instance;cls.lpszClassName=L"GMEngineEditor";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClassW(&cls);
        RECT bounds{0,0,1440,900};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"GM Engine",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
        if(!window) throw std::runtime_error("Cannot create editor window");
        Editor editor(window,project,session,gameProject);app=&editor;ShowWindow(window,SW_SHOW);UpdateWindow(window);
        MSG msg{};bool running=true;
        while(running) {while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {if(msg.message==WM_QUIT) {running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(running) {editor.frame();MsgWaitForMultipleObjectsEx(0,nullptr,editor.idleWait(),QS_ALLINPUT,MWMO_INPUTAVAILABLE);}}
        app=nullptr;return 0;
    } catch(const std::exception& e) {app=nullptr;MessageBoxW(nullptr,gm::wide(e.what()).c_str(),L"GM Engine",MB_OK|MB_ICONERROR);return 1;}
}
