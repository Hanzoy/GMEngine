#include "project.hpp"
#include <iostream>
#include <bit>
#include <limits>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    check(argc==2,"module required");auto registry=std::make_shared<gm::GameRegistry>();registry->load(argv[1]);gm::useGameRegistry(registry);
    auto source=gm::newScene();source["entities"].push_back({{"id","probe"},{"name","Probe"},{"parent",nullptr},{"components",gm::Json::array({{{"type","test.Input"},{"version",1},{"fields",{{"clicks",0},{"keys",0},{"wheel",0}}}}})}});
    gm::SceneRuntime run(gm::compileScene(source,{}));gm::SceneInput input;input.pointer.pressed=1;input.pointer.wheel=2;input.pointer.keyPress['R'/32]=1u<<('R'%32);
    check(run.advance(1./240,input)==0,"sub-step render must wait");check(run.advance(1./240,{})==0,"input must remain queued");check(run.advance(1./120,{})==1,"queued input delivered");auto words=run.component(0,gm::sdk::id("test.Input"));check(words[0]==1&&words[1]==1&&std::bit_cast<float>(words[2])==2,"short click/key/wheel not lost");
    check(run.canvas().size()==1&&std::string(run.canvas()[0].text)=="copied text","draw data owns copied strings");
    run.advance(3./60,input);words=run.component(0,gm::sdk::id("test.Input"));check(words[0]==2&&words[1]==2&&std::bit_cast<float>(words[2])==4,"catch-up must not repeat edges");check(run.canvas().size()==1,"canvas clears each tick");
    gm::runtimeCommand(run,{{"op","runtime.input"},{"pressed",1},{"frames",4}});check(run.component(0,gm::sdk::id("test.Input"))[0]==3,"CLI repeats held input but edges once");
    bool rejected=false;try{gm::runtimeCommand(run,{{"op","runtime.input"},{"keyPress",gm::Json::array({300})}});}catch(...){rejected=true;}check(rejected,"reject invalid key");
    input.pointer.mouseX=std::numeric_limits<float>::quiet_NaN();rejected=false;try{run.tick(1.f/60,input);}catch(...){rejected=true;}check(rejected,"reject nonfinite pointer");
    input={};input.pointer.keyPress['X'/32]=1u<<('X'%32);rejected=false;try{run.tick(1.f/60,input);}catch(...){rejected=true;}check(rejected,"reject malformed canvas command");
    run.reset();run.tick(1.f/60,{});check(run.component(0,gm::sdk::id("test.Input"))[0]==0,"reset clears input state");std::cout<<"PASS input edges, frame retention, canvas ownership, CLI injection, validation, reset\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
