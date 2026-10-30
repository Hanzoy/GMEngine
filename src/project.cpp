#include "project.hpp"
#include "process.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace gm {
Json recentProjects(){auto result=Json::array();try{auto data=readJson(userDataRoot()/"projects.json");if(data.is_array())for(auto item:data){if(!item.is_object()||!item.contains("path")||!item["path"].is_string()||!item.contains("name")||!item["name"].is_string())continue;auto text=item["path"].get<std::string>();std::error_code ec;item["exists"]=std::filesystem::exists(std::filesystem::path(std::u8string(text.begin(),text.end())),ec);result.push_back(item);if(result.size()==30)break;}}catch(...){}return result;}
void forgetProject(const std::filesystem::path& manifest){auto items=recentProjects();auto path=std::filesystem::absolute(manifest).lexically_normal().generic_u8string();auto text=std::string(path.begin(),path.end());items.erase(std::remove_if(items.begin(),items.end(),[&](const Json& item){return item.at("path")==text;}),items.end());writeJsonAtomic(userDataRoot()/"projects.json",items);}
void rememberProject(const GameProject& project){if(!project.valid())return;forgetProject(project.manifest);auto items=recentProjects();auto path=project.manifest.generic_u8string();items.insert(items.begin(),Json{{"name",project.name},{"path",std::string(path.begin(),path.end())}});if(items.size()>30)items.erase(items.begin()+30,items.end());writeJsonAtomic(userDataRoot()/"projects.json",items);}
namespace {
std::string textPath(const std::filesystem::path& p){auto u=p.u8string();return std::string(u.begin(),u.end());}
std::filesystem::path utfpath(const std::string& p){return std::filesystem::path(std::u8string(p.begin(),p.end()));}
std::filesystem::path memberPath(const std::filesystem::path& root,const std::string& name){auto rel=utfpath(name);if(rel.empty()||rel.is_absolute())throw std::runtime_error("Project paths must be relative");auto p=std::filesystem::weakly_canonical(root/rel);auto relative=p.lexically_relative(std::filesystem::canonical(root));if(relative.empty()||*relative.begin()=="..")throw std::runtime_error("Project path escapes project root");return p;}
std::string sourceText(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read "+textPath(path));std::string s((std::istreambuf_iterator<char>(in)),{});s.erase(std::remove(s.begin(),s.end(),'\r'),s.end());return s;}
bool restoreTemplateModule(const GameProject& p){
    auto source=engineRoot()/"sdk/templates/Basic";
    auto binary=source/"bin/windows-x64/Game.dll";
    // Only recover our unchanged starter sources. Never substitute this module for custom game code.
    if(!std::filesystem::is_regular_file(binary)||!std::filesystem::is_regular_file(p.root/"Scripts/game.cpp")||!std::filesystem::is_regular_file(p.root/"CMakeLists.txt"))return false;
    if(p.module!=memberPath(p.root,"bin/windows-x64/Game.dll")&&p.module!=memberPath(p.root,"build/Release/Game.dll"))return false;
    if(sourceText(p.root/"Scripts/game.cpp")!=sourceText(source/"Scripts/game.cpp"))return false;
    auto cmake=sourceText(p.root/"CMakeLists.txt");
    const std::string legacy="cmake_minimum_required(VERSION 3.21)\nproject(Game LANGUAGES CXX)\nfind_package(GMEngine CONFIG REQUIRED)\ngm_add_game_module(Game Scripts/game.cpp)\n";
    if(cmake!=legacy&&cmake!=sourceText(source/"CMakeLists.txt"))return false;
    std::filesystem::create_directories(p.module.parent_path());
    std::filesystem::copy_file(binary,p.module);
    return true;
}
std::filesystem::path cmakeExecutable(){
#ifdef _WIN32
    auto environment=[](const wchar_t* name){wchar_t value[32768]{};auto n=GetEnvironmentVariableW(name,value,32768);return n&&n<32768?std::filesystem::path(value):std::filesystem::path();};
    auto regular=[](const std::filesystem::path& path){std::error_code ec;return std::filesystem::is_regular_file(path,ec);};
    if(auto configured=environment(L"GM_ENGINE_CMAKE");!configured.empty()){
        if(regular(configured))return std::filesystem::absolute(configured);
        throw std::runtime_error("GM_ENGINE_CMAKE 指向的 CMake 不存在："+textPath(configured)+"。请将其设置为 cmake.exe 的完整路径。");
    }
    wchar_t found[32768]{};auto n=SearchPathW(nullptr,L"cmake.exe",nullptr,32768,found,nullptr);
    if(n&&n<32768&&regular(found))return found;
    for(const auto* variable:{L"ProgramFiles",L"ProgramFiles(x86)"}){
        auto programs=environment(variable);if(programs.empty())continue;
        auto standard=programs/L"CMake/bin/cmake.exe";if(regular(standard))return standard;
        std::error_code ec;auto visualStudio=programs/L"Microsoft Visual Studio";
        for(const auto& version:std::filesystem::directory_iterator(visualStudio,ec)){
            std::error_code nestedError;
            for(const auto& edition:std::filesystem::directory_iterator(version.path(),nestedError)){
                auto bundled=edition.path()/L"Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
                if(regular(bundled))return bundled;
            }
        }
    }
    throw std::runtime_error("此项目需要编译 C++ 游戏模块，但未找到 CMake。\n新建模板项目、编辑场景和运行已编译项目不需要 CMake。\n修改 C++ 或打开缺少游戏 DLL 的自定义项目，需要 Visual Studio 2022 的‘使用 C++ 的桌面开发’工具与 CMake。可安装‘用于 Windows 的 C++ CMake 工具’，或设置 GM_ENGINE_CMAKE 为 cmake.exe 的完整路径。");
#else
    throw std::runtime_error("Native project build service currently supports Windows");
#endif
}
void run(const std::vector<std::wstring>& args,const std::filesystem::path& cwd,const std::filesystem::path& log){
    if(runProcess(args,cwd,log,{},true))throw std::runtime_error("Project build failed; see "+textPath(log));
}
}
std::filesystem::path engineRoot(){
    auto valid=[](const std::filesystem::path& p){return std::filesystem::exists(p/"engine-sdk.json")&&std::filesystem::exists(p/"sdk/cmake/GMEngineConfig.cmake");};
    std::filesystem::path executable;
#ifdef _WIN32
    wchar_t path[32768]{};auto n=GetModuleFileNameW(nullptr,path,32768);if(n&&n<32768)executable=path;
#elif defined(__linux__)
    std::error_code ec;executable=std::filesystem::read_symlink("/proc/self/exe",ec);
#endif
    if(!executable.empty()){auto folder=executable.parent_path();if(valid(folder))return folder;if(valid(folder.parent_path()))return folder.parent_path();}
    // Uninstalled developer/test binaries can use the configured installation, never the source tree.
    auto configured=std::filesystem::path(GM_ENGINE_INSTALL_DIR);if(valid(configured))return configured;
    throw std::runtime_error("Engine SDK not installed; run scripts/build.ps1 or cmake --install BUILD --config Release");
}
GameProject readProject(const std::filesystem::path& path){auto manifest=std::filesystem::canonical(path);auto data=readJson(manifest);if(data.value("format",std::string())!="gm.project"||data.at("version")!=1)throw std::runtime_error("Expected gm.project version 1");auto name=data.at("name").get<std::string>();if(name.empty()||name.size()>128)throw std::runtime_error("Invalid project name");auto root=manifest.parent_path();auto entry=memberPath(root,data.at("entry").get<std::string>());auto module=memberPath(root,data.at("module").get<std::string>());if(!entry.u8string().ends_with(u8".scene.json"))throw std::runtime_error("Project entry must be a scene resource");return {manifest,root,entry,module,name};}
std::shared_ptr<GameRegistry> loadProjectModule(const GameProject& p){auto registry=std::make_shared<GameRegistry>();if(p.valid())registry->load(p.module);return registry;}
GameProject activateProject(std::filesystem::path& resource,const std::filesystem::path& explicitProject){GameProject p;resource=std::filesystem::absolute(resource).lexically_normal();if(!explicitProject.empty())p=readProject(explicitProject);else if(resource.extension()==".gmproject")p=readProject(resource);else{auto folder=resource.parent_path();while(!folder.empty()){auto manifest=folder/"game.gmproject";if(std::filesystem::exists(manifest)){p=readProject(manifest);break;}auto parent=folder.parent_path();if(parent==folder)break;folder=parent;}}
    if(p.valid()&&!std::filesystem::exists(p.module)&&!restoreTemplateModule(p))buildProjectModule(p);try{useGameRegistry(loadProjectModule(p));}catch(const std::exception& error){if(!p.valid()||std::string(error.what()).find("SDK ABI mismatch")==std::string::npos)throw;buildProjectModule(p);useGameRegistry(loadProjectModule(p));}if(resource.extension()==".gmproject")resource=p.entry;return p;
}
Json projectDescription(const GameProject& p){return {{"name",p.name},{"manifest",textPath(p.manifest)},{"root",textPath(p.root)},{"entry",textPath(p.entry)},{"module",textPath(p.module)},{"moduleLoaded",gameRegistry()->name()},{"sdkAbi",sdk::AbiVersion},{"schema",gameRegistry()->fingerprint()},{"components",sceneSchema()["components"]},{"systems",systemSchema(Json::object())}};}
void buildProjectModule(const GameProject& p){auto operation=lockProjectOperation(p);if(!p.valid())throw std::runtime_error("Open a .gmproject first");auto cmake=cmakeExecutable();auto build=p.root/"build";std::filesystem::create_directories(build);auto log=build/"native-build.log";{std::ofstream clear(log,std::ios::trunc);}run({cmake.wstring(),L"-S",p.root.wstring(),L"-B",build.wstring(),L"-G",L"Visual Studio 17 2022",L"-A",L"x64",L"-DGMEngine_DIR="+(engineRoot()/"sdk/cmake").wstring()},p.root,log);run({cmake.wstring(),L"--build",build.wstring(),L"--config",L"Release",L"--parallel",L"4"},p.root,log);if(!std::filesystem::exists(p.module))throw std::runtime_error("Project module output does not match manifest");}
void compileProject(const GameProject& p){buildProjectAssets(p);}
Json packageProject(const GameProject& p){auto operation=lockProjectOperation(p);compileProject(p);auto output=p.root/"dist";std::filesystem::create_directories(output/"licenses");auto copy=[&](auto from,auto to){std::filesystem::copy_file(from,to,std::filesystem::copy_options::overwrite_existing);};copy(engineRoot()/"bin/gm_player.exe",output/"gm_player.exe");copy(p.module,output/"game.dll");copy(p.root/"build/game.gmscene",output/"game.gmscene");copy(p.root/"build/scenes.gmcatalog",output/"scenes.gmcatalog");std::filesystem::copy(p.root/"build/scenes",output/"scenes",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);for(const auto* name:{"bgfx","bx","bimg","flecs"})copy(engineRoot()/"licenses"/(std::string(name)+".txt"),output/"licenses"/(std::string(name)+".txt"));return {{"directory",textPath(output)},{"executable",textPath(output/"gm_player.exe")}};}
Json createProject(const std::filesystem::path& directory,const std::string& name){if(name.empty()||name.size()>128)throw std::runtime_error("Invalid project name");auto dest=std::filesystem::absolute(directory).lexically_normal();if(std::filesystem::exists(dest))throw std::runtime_error("New project directory must not exist");auto source=engineRoot()/"sdk/templates/Basic";if(!std::filesystem::is_regular_file(source/"bin/windows-x64/Game.dll"))throw std::runtime_error("安装包缺少已编译的项目模板，请重新安装 GM Engine 0.8.1 或更新版本。");std::filesystem::create_directories(dest);for(const auto& file:std::filesystem::recursive_directory_iterator(source)){auto target=dest/file.path().lexically_relative(source);if(file.is_directory())std::filesystem::create_directories(target);else if(file.is_regular_file())std::filesystem::copy_file(file.path(),target);}auto data=readJson(dest/"game.gmproject");data["name"]=name;writeJsonAtomic(dest/"game.gmproject",data);return {{"manifest",textPath(dest/"game.gmproject")}};}
Json runtimeState(SceneRuntime& run,bool paused,uint64_t revision,bool dirty){Json entities=Json::array();for(uint32_t i=0;i<run.entities().size();++i){if(!run.alive(i))continue;const auto& entity=run.entities()[i];auto p=run.position(i);Json components=Json::array();for(const auto& c:entity.components){const auto& d=gameRegistry()->component(c.type).descriptor;components.push_back({{"type",d.name},{"version",d.version},{"fields",componentJson(c.type,run.component(i,c.type))}});}entities.push_back({{"id",entity.id},{"handle",run.entity(i)},{"x",p.x},{"y",p.y},{"components",components}});}Json bindings=Json::array();for(const auto& b:run.asset().bindings){auto& n=run.hud().nodes()[b.node];bindings.push_back({{"node",b.node},{"entity",run.asset().entities[b.entity].id},{"kind",b.kind==BindingKind::NumberText?"number.text":b.kind==BindingKind::NumberBar?"number.bar":"number.change"},{"amount",b.amount},{"text",n.text},{"width",n.bounds.w},{"visible",run.hud().visible(b.node)}});}return {{"ok",true},{"paused",paused},{"ticks",run.ticks()},{"entities",entities},{"uiBindings",bindings},{"revision",revision},{"dirty",dirty}};}
void addProjectCommands(Json& result){auto& cmds=result["commands"];cmds["project.assets"]="Import/index assets and return stable IDs";cmds["runtime.scene.switch"]="scene: stable ID or catalog scene name";cmds["runtime.query.area"]="x,y,w,h, optional mask: SDK Entity handles intersecting an AABB";cmds["runtime.query.ray"]="x,y,dx,dy,length, optional mask: closest hit";for(auto name:{"runtime.entity.create","runtime.entity.destroy","runtime.component.set","runtime.component.remove","runtime.prefab.instantiate","runtime.stats","runtime.save","runtime.load"})cmds[name]="Runtime service; see docs/engine-v08.md";cmds["project.recent"]="Recent projects";cmds["project.forget"]="path; remove recent entry without deleting project";cmds["project.query"]="Project paths, module and schema";cmds["project.create"]="directory, name; create an independent native game project";cmds["project.open"]="path to .gmproject; stop and save before switching";cmds["project.build"]="Build and reload native module, then compile saved entry scene; stop and save first";cmds["project.reload"]="Reload built module while stopped; all open documents must validate";cmds["project.package"]="Package saved project with Player, native module and compiled scene";cmds["file.open"]="path; switch resource (unsaved changes require discard:true in CLI)";cmds["system.list"]="Ordered Systems and enabled states";cmds["system.set"]="name, enabled; undoable scene configuration";cmds["binding.set"]="node, binding (entity, component, field, kind, optional maximum/amount)";cmds["binding.remove"]="node; remove scene-to-UI binding";cmds["preview.run"]="Compile current in-memory scene; CLI starts paused, editor accepts paused:true";cmds["preview.pause"]="paused: boolean; editor can advance in real time, headless CLI uses explicit stepping";cmds["preview.stop"]="Destroy simulation, retain authoring data";cmds["runtime.query"]="All runtime component values and HUD bindings";cmds["runtime.step"]="Paused simulation: frames (1..10000), x, y; fixed 1/60 s";cmds["runtime.activate"]="node; invoke a bound UI action";cmds["runtime.entity.create"]="Optional parent stable ID; returns a runtime stable ID";cmds["runtime.entity.destroy"]="id: runtime stable ID; destroys its subtree";cmds["runtime.component.set"]="id,type,fields: complete fields; images use compiled indices";cmds["runtime.component.remove"]="id,type: dependency-checked removal";cmds["runtime.prefab.instantiate"]="source: authored entity ID, optional parent ID";cmds["runtime.save"]="slot: optional safe save name";cmds["runtime.load"]="slot: optional safe save name; replaces runtime World";cmds["runtime.stats"]="Current simulation time, System timings and live counts";result["systems"]=systemSchema(Json::object());}
}
