#include "project.hpp"
#include "path_text.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
namespace { std::filesystem::path utfpath(const std::string& value){return std::filesystem::path(std::u8string(value.begin(),value.end()));}
class Session {
    std::shared_ptr<void> initialBuildLock_;gm::GameProject project_;std::unique_ptr<gm::Document> document_;std::unique_ptr<gm::SceneRuntime> runtime_;bool paused_=true,preparedBuild_=false;
public:
    explicit Session(std::filesystem::path path,const std::filesystem::path& game,bool buildFirst=false){
        // A broken previous DLL must not prevent the CLI from rebuilding its source.
        if(buildFirst){auto manifest=game;if(manifest.empty()&&path.extension()==".gmproject")manifest=path;if(manifest.empty()){auto dir=std::filesystem::absolute(path).parent_path();while(!dir.empty()){if(std::filesystem::exists(dir/"game.gmproject")){manifest=dir/"game.gmproject";break;}auto parent=dir.parent_path();if(parent==dir)break;dir=parent;}}if(!manifest.empty()){auto project=gm::readProject(manifest);initialBuildLock_=gm::lockProjectOperation(project);gm::buildProjectModule(project);preparedBuild_=true;}}
        project_=gm::activateProject(path,game);document_=std::make_unique<gm::Document>(path);
    }
    gm::Json execute(const gm::Json& request){try{auto op=request.at("op").get<std::string>();auto preparedLock=std::move(initialBuildLock_);auto operation=(op=="project.build"||op=="project.package"||op=="project.assets")?gm::lockProjectOperation(project_):std::shared_ptr<void>{};auto& doc=*document_;if(request.contains("expected_revision")&&request["expected_revision"]!=doc.revision())throw std::runtime_error("Revision conflict; query again");
        if(op=="project.query")return {{"ok",true},{"result",gm::projectDescription(project_)}};
        if(op=="project.recent")return {{"ok",true},{"result",gm::recentProjects()}};
        if(op=="project.assets")return {{"ok",true},{"result",gm::importProjectAssets(project_)}};
        if(op=="project.forget"){gm::forgetProject(utfpath(request.at("path").get<std::string>()));return {{"ok",true}};}
        if(op=="project.create")return {{"ok",true},{"result",gm::createProject(utfpath(request.at("directory").get<std::string>()),request.at("name").get<std::string>())}};
        if(op=="project.build"||op=="project.reload"||op=="project.package"){
            if(runtime_||doc.dirty())throw std::runtime_error("Stop simulation and save edits before project build/reload/package");if(!project_.valid())throw std::runtime_error("Open a .gmproject first");
            if(op=="project.build"){if(!preparedBuild_)gm::buildProjectModule(project_);preparedBuild_=false;}auto previous=gm::gameRegistry();auto next=gm::loadProjectModule(project_);gm::useGameRegistry(next);try{gm::validateDocument(doc.data());}catch(...){gm::useGameRegistry(previous);throw;}
            document_=std::make_unique<gm::Document>(doc.path());gm::Json result=gm::projectDescription(project_);if(op=="project.build")gm::compileProject(project_);if(op=="project.package")result=gm::packageProject(project_);return {{"ok",true},{"result",result}};
        }
        if(op=="project.open"){if(runtime_||doc.dirty())throw std::runtime_error("Stop and save before opening another project");auto path=utfpath(request.at("path").get<std::string>());auto previous=gm::gameRegistry();try{auto nextProject=gm::activateProject(path);auto nextDoc=std::make_unique<gm::Document>(path);project_=std::move(nextProject);document_=std::move(nextDoc);}catch(...){gm::useGameRegistry(previous);throw;}return {{"ok",true},{"result",gm::projectDescription(project_)}};}
        if(op=="file.open"){if(runtime_)throw std::runtime_error("Stop before opening a resource");if(doc.dirty()&&!request.value("discard",false))throw std::runtime_error("Unsaved edits; save or pass discard:true");auto path=utfpath(request.at("path").get<std::string>());if(path.is_relative())path=(project_.valid()?project_.root:doc.path().parent_path())/path;auto next=std::make_unique<gm::Document>(path);document_=std::move(next);return {{"ok",true},{"result",{{"path",gm::pathText(path)}}}};}
        if(op=="preview.run"){if(runtime_)throw std::runtime_error("Scene already running");runtime_=std::make_unique<gm::SceneRuntime>(gm::compileScene(doc.data(),doc.path().parent_path()));paused_=true;return gm::runtimeState(*runtime_,paused_,doc.revision(),doc.dirty());}
        if(op=="preview.stop"){runtime_.reset();return {{"ok",true},{"revision",doc.revision()},{"dirty",doc.dirty()}};}
        if(op=="runtime.query"||op=="runtime.step"||op=="preview.step"||op=="runtime.activate"||op=="preview.pause"){
            if(!runtime_)throw std::runtime_error("Scene is not running");if(op=="preview.pause")paused_=request.value("paused",!paused_);
            if(op=="runtime.step"||op=="preview.step"){if(!paused_)throw std::runtime_error("Pause before explicit stepping");auto frames=request.value("frames",1);if(frames<1||frames>10000)throw std::runtime_error("frames must be 1..10000");for(int i=0;i<frames;++i){runtime_->tick(1.f/60,{request.value("x",0.f),request.value("y",0.f)});auto next=runtime_->takeSceneRequest();if(!next.empty())gm::runtimeCommand(*runtime_,{{"op","runtime.scene.switch"},{"scene",next}},project_);}}
            if(op=="runtime.activate"&&!runtime_->activateUi(request.at("node").get<uint32_t>()))throw std::runtime_error("UI action not found");return gm::runtimeState(*runtime_,paused_,doc.revision(),doc.dirty());
        }
        if(op.starts_with("runtime.")){if(!runtime_)throw std::runtime_error("Scene is not running");return gm::runtimeCommand(*runtime_,request,project_);}
        if(runtime_&&op!="query"&&op!="describe"&&op!="system.list"&&op!="select"&&op!="save")throw std::runtime_error("Stop simulation before authoring edits");auto result=doc.execute(request);if(op=="describe"&&result.value("ok",false)){gm::addProjectCommands(result["result"]);result["result"]["project"]=gm::projectDescription(project_);}return result;
    }catch(const std::exception& e){return {{"ok",false},{"error",e.what()}};}}
};
}
int runCli(int argc,char** argv){try{
    std::filesystem::path project,game,live,requestFile;std::string raw;bool stdioMode=false,hub=false;
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--help"){std::cout<<"gmctl --project GAME.gmproject|RESOURCE [--game GAME.gmproject] [--stdio|--request FILE|--command JSON]\ngmctl --live SESSION [--request FILE|--command JSON]\ngmctl --hub [--stdio|--request FILE|--command JSON]\nNative scene simulation works headlessly; preview.run starts paused.\n";return 0;}if(arg=="--hub"){hub=true;continue;}if(arg=="--stdio"){stdioMode=true;continue;}if(i+1>=argc)throw std::runtime_error("Missing argument value");if(arg=="--project")project=utfpath(argv[++i]);else if(arg=="--game")game=utfpath(argv[++i]);else if(arg=="--live")live=utfpath(argv[++i]);else if(arg=="--request")requestFile=utfpath(argv[++i]);else if(arg=="--command")raw=argv[++i];else throw std::runtime_error("Unknown argument: "+arg);}
    if(int(!project.empty())+int(!live.empty())+int(hub)!=1)throw std::runtime_error("Choose exactly one of --project, --live or --hub");if(stdioMode&&(!live.empty()||!requestFile.empty()||!raw.empty()))throw std::runtime_error("--stdio requires offline project and stdin");
    if(stdioMode){std::unique_ptr<Session> session;std::string line;while(std::getline(std::cin,line)){try{auto request=gm::Json::parse(line);if(hub){std::cout<<gm::hubCommand(request).dump()<<std::endl;continue;}if(!session)session=std::make_unique<Session>(project,game,request.value("op",std::string())=="project.build");std::cout<<session->execute(request).dump()<<std::endl;}catch(const std::exception& e){std::cout<<gm::Json({{"ok",false},{"error",e.what()}}).dump()<<std::endl;}}return 0;}
    gm::Json request;if(!requestFile.empty())request=gm::readJson(requestFile);else{if(raw.empty())std::getline(std::cin,raw);request=gm::Json::parse(raw);}gm::Json response;
    if(hub)response=gm::hubCommand(request);else if(!project.empty()){Session session(project,game,request.value("op",std::string())=="project.build");response=session.execute(request);}else{
        if(!std::filesystem::exists(live/"session.json"))throw std::runtime_error("No live editor session at this path");const auto id=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());const auto inbox=live/"inbox"/(id+".json"),outbox=live/"outbox"/(id+".json");gm::writeJsonAtomic(inbox,request);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(request.value("op",std::string()).starts_with("project.")?180:10);while(!std::filesystem::exists(outbox)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));if(!std::filesystem::exists(outbox))throw std::runtime_error("Editor response timed out; outcome unknown. Query before retrying. Request: "+id);response=gm::readJson(outbox);std::filesystem::remove(outbox);
    }std::cout<<response.dump(2)<<'\n';return response.value("ok",false)?0:1;
}catch(const std::exception& e){std::cout<<gm::Json({{"ok",false},{"error",e.what()}}).dump()<<'\n';return 1;}}

#ifdef _WIN32
int wmain(int argc,wchar_t** wide){std::vector<std::string> text;std::vector<char*> args;for(int i=0;i<argc;++i){auto utf=std::filesystem::path(wide[i]).u8string();text.emplace_back(utf.begin(),utf.end());}for(auto& a:text)args.push_back(a.data());return runCli(argc,args.data());}
#else
int main(int argc,char** argv){return runCli(argc,argv);}
#endif
