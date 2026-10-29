#include "project.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
bool near(float a,float b){return std::abs(a-b)<.02f;}
}
int main(int argc,char** argv){std::filesystem::path temp;
    try{
        check(argc==2,"Pass the SDK fixture module path");
        auto registry=std::make_shared<gm::GameRegistry>();registry->load(argv[1]);gm::useGameRegistry(registry);
        const auto root=std::filesystem::path(GM_SOURCE_DIR);
        auto source=gm::readJson(root/"sdk/templates/Basic/assets/main.scene.json");
        temp=std::filesystem::temp_directory_path()/("gm-scene-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(temp);
        auto hud=gm::readJson(root/"tests/fixtures/ui/counter.ui.json");hud["root"]["w"]=1280;hud["root"]["h"]=720;gm::writeJsonAtomic(temp/"hud.ui.json",hud);
        source["ui"]={{"source","hud.ui.json"},{"bindings",gm::Json::array({{{"node","count"},{"entity","player"},{"component","game.Mover"},{"field","speed"},{"kind","number.text"}},{{"node","add"},{"entity","player"},{"component","game.Mover"},{"field","speed"},{"kind","number.change"},{"amount",10}}})}};
        auto scene=gm::compileScene(source,temp);auto bytes=gm::encodeScene(scene);check(gm::encodeScene(gm::decodeScene(bytes))==bytes,"Binary roundtrip");
        auto invalid=bytes;invalid.back()^=1;rejects([&]{gm::decodeScene(invalid);});invalid=bytes;invalid[4]=1;rejects([&]{gm::decodeScene(invalid);});rejects([&]{gm::decodeScene(std::span(bytes).first(20));});
        auto bad=scene;bad.schema^=1;rejects([&]{gm::validateSceneBinary(bad);});bad=scene;bad.entities[0].parent=0;rejects([&]{gm::validateSceneBinary(bad);});bad=scene;bad.entities[0].components[0].version++;rejects([&]{gm::validateSceneBinary(bad);});
        gm::SceneRuntime run(scene);auto index=run.find("player");check(index!=gm::NoIndex,"Stable entity identity");auto start=run.position(index);for(int i=0;i<60;++i)run.tick(1.f/60,{1,0});check(near(run.position(index).x,start.x+100),"SDK module writes velocity, engine integrates it");
        run.reset();auto paint=run.hud().paintRevision();run.tick(1.f/60,{});check(run.hud().paintRevision()==paint,"Unchanged numeric binding invalidated UI");check(run.activateUi(scene.bindings[1].node),"Generic numeric action");check(run.hud().nodes()[scene.bindings[0].node].text=="110","Generic project-field HUD binding");
        gm::SceneRuntime other(scene);check(other.hud().nodes()[scene.bindings[0].node].text=="100","World isolation");auto next=std::make_shared<gm::GameRegistry>();next->load(argv[1]);gm::useGameRegistry(next);run.tick(1.f/60,{});check(run.ticks()==2,"Existing world keeps its module alive");
        bad=scene;bad.disabledSystems.push_back("game.Move");gm::SceneRuntime disabled(bad);disabled.tick(.1f,{1,0});check(near(disabled.position(index).x,start.x),"System disablement");
        auto child=source["entities"][0];child["id"]="child";child["parent"]="player";child["components"]=gm::Json::array({{{"type","Transform2D"},{"version",1},{"fields",{{"x",10},{"y",20}}}}});auto hierarchy=source;hierarchy["entities"].push_back(child);gm::SceneRuntime tree(gm::compileScene(hierarchy,temp));check(near(tree.position(tree.find("child")).x,410),"Parent transform composition");
        gm::writeJsonAtomic(temp/"test.scene.json",source);gm::Document doc(temp/"test.scene.json");auto ok=[&](gm::Json request){auto result=doc.execute(request);if(!result.value("ok",false))throw std::runtime_error(result.dump());return result;};
        ok({{"op","set"},{"id","player"},{"property","game.Mover.speed"},{"value",150}});ok({{"op","undo"}});check(doc.data()==source,"Namespaced field undo");ok({{"op","system.set"},{"name","game.Move"},{"enabled",false}});ok({{"op","undo"}});check(doc.data()==source,"System configuration undo");
        check(!doc.execute({{"op","component.remove"},{"id","player"},{"type","Transform2D"}})["ok"].get<bool>(),"Component dependencies");check(!doc.execute({{"op","component.remove"},{"id","player"},{"type","game.Mover"}})["ok"].get<bool>(),"Bound component removal");
        auto entity=ok({{"op","entity.create"}})["result"]["id"];ok({{"op","component.add"},{"id",entity},{"type","game.Mover"}});check(gm::findEntity(doc.data(),entity)->at("components").size()==3,"Composition auto-adds dependencies");
        rejects([&]{run.tick(-1,{});});std::filesystem::remove_all(temp);std::cout<<"SDK registration, ECS composition, System scheduling, native lifetime, binary validation, generic HUD and undo passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\nFixture: "<<temp<<'\n';return 1;}
}
