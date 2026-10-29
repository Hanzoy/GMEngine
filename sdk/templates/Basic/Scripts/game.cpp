#include <gm/game.hpp>
using namespace gm::sdk;
struct Mover {static constexpr const char* gm_name="game.Mover";float speed=100;};
namespace {
const Field fields[]={GM_FLOAT(Mover,speed,"移动速度",100,0,1000)};
const char* required[]={"Transform2D","Velocity"};
const Component components[]={component<Mover>("Mover",fields,1,required,2)};
void move(void*,const Context* c){c->each<Velocity,const Mover>([&](Entity,Velocity& v,const Mover& m){v={c->frame->inputX*m.speed,c->frame->inputY*m.speed};});}
const System systems[]={{"game.Move","移动",Phase::Input,nullptr,0,nullptr,nullptr,move}};
const Module module{AbiVersion,sizeof(Module),"Game",components,1,systems,1};
}
GM_GAME_EXPORT const Module* gm_game_module(uint32_t abi){return abi==AbiVersion?&module:nullptr;}
