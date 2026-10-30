#include <gm/game.hpp>
#include <cstring>
#include <limits>
using namespace gm::sdk;
struct Probe {static constexpr const char* gm_name="test.Input";int32_t clicks=0,keys=0;float wheel=0;};
namespace {
const Field fields[]={GM_INT(Probe,clicks,"Clicks",0,0,100),GM_INT(Probe,keys,"Keys",0,0,100),GM_FLOAT(Probe,wheel,"Wheel",0,-100,100)};
const Component components[]={component<Probe>("Input",fields,3)};
void update(void*,const Context* c){const auto& input=c->frame->input;c->each<Probe>([&](Entity,Probe& p){p.clicks+=bool(input.pressed&1);p.keys+=input.hit('R');p.wheel+=input.wheel;});CanvasCommand draw;draw.kind=CanvasKind::Text;draw.w=200;draw.size=16;std::strcpy(draw.text,"copied text");c->draw(draw);draw.text[0]='X';if(input.hit('X')){draw.x=std::numeric_limits<float>::quiet_NaN();c->draw(draw);}}
const System systems[]={{"test.Input","Input",Phase::Input,nullptr,0,nullptr,nullptr,update}};
const Module module{AbiVersion,sizeof(Module),"InputContract",components,1,systems,1};
}
GM_GAME_EXPORT const Module* gm_game_module(uint32_t abi){return abi==AbiVersion?&module:nullptr;}
