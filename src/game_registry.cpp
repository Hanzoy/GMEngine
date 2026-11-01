#include "game_registry.hpp"
#include "path_text.hpp"
#include "storage.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace gm {
namespace {
using namespace sdk;
#include "render_components.inl"
const Field transformFields[]={GM_FLOAT(Transform2D,x,"位置 X",0,-16384,16384),GM_FLOAT(Transform2D,y,"位置 Y",0,-16384,16384)};
const Field velocityFields[]={GM_FLOAT(Velocity,x,"速度 X",0,-5000,5000),GM_FLOAT(Velocity,y,"速度 Y",0,-5000,5000)};
const Field spriteFields[]={
    {"image","图片",Kind::Image,offsetof(SpriteRenderer,image),UINT32_MAX,0,UINT32_MAX},
    GM_FLOAT(SpriteRenderer,width,"宽度",64,1,4096),GM_FLOAT(SpriteRenderer,height,"高度",64,1,4096),
    {"tint","着色",Kind::Color,offsetof(SpriteRenderer,tint),0xffffffff,0,UINT32_MAX},GM_FLOAT(SpriteRenderer,order,"绘制顺序",0,-1000,1000)};
const char* transformDependency[]={"Transform2D"};
const Field poseFields[]={GM_FLOAT(Pose2D,rotation,"旋转角度",0,-360000,360000),GM_FLOAT(Pose2D,scaleX,"缩放 X",1,.001,1000),GM_FLOAT(Pose2D,scaleY,"缩放 Y",1,.001,1000)};
const Field cameraFields[]={GM_FLOAT(Camera2D,zoom,"缩放",1,.01,100),GM_BOOL(Camera2D,enabled,"启用",1),GM_INT(Camera2D,priority,"优先级",0,-1000,1000)};
const Field regionFields[]={GM_FLOAT(SpriteRegion,u0,"U 起点",0,0,1),GM_FLOAT(SpriteRegion,v0,"V 起点",0,0,1),GM_FLOAT(SpriteRegion,u1,"U 终点",1,0,1),GM_FLOAT(SpriteRegion,v1,"V 终点",1,0,1)};
const Field colliderFields[]={GM_FLOAT(Collider2D,width,"宽度",64,.001,16384),GM_FLOAT(Collider2D,height,"高度",64,.001,16384),GM_FLOAT(Collider2D,offsetX,"偏移 X",0,-16384,16384),GM_FLOAT(Collider2D,offsetY,"偏移 Y",0,-16384,16384),GM_INT(Collider2D,layer,"碰撞层位掩码",1,1,65535),GM_INT(Collider2D,mask,"接收层位掩码",65535,0,65535),GM_BOOL(Collider2D,enabled,"启用",1)};
const Field animationFields[]={GM_INT(SpriteAnimation,columns,"图集列数",1,1,256),GM_INT(SpriteAnimation,rows,"图集行数",1,1,256),GM_INT(SpriteAnimation,frames,"帧数",1,1,65536),GM_FLOAT(SpriteAnimation,fps,"帧率",12,0,240),GM_FLOAT(SpriteAnimation,time,"播放时间",0,0,1000000),GM_BOOL(SpriteAnimation,playing,"播放",1),GM_BOOL(SpriteAnimation,loop,"循环",1)};
const char* animationDependencies[]={"SpriteRenderer","SpriteRegion"};
bool validAnimation(const void* ptr,char* error,uint32_t capacity){auto& a=*static_cast<const SpriteAnimation*>(ptr);if(a.frames<=a.columns*a.rows)return true;if(capacity)std::snprintf(error,capacity,"frames exceeds atlas cells");return false;}
void animate(void*,const Context* context){context->each<SpriteAnimation,SpriteRegion>([&](Entity,SpriteAnimation& a,SpriteRegion& r){if(a.playing)a.time+=context->frame->dt;auto frame=int64_t(a.time*a.fps);if(a.loop)frame%=a.frames;else if(frame>=a.frames){frame=a.frames-1;a.playing=0;}r={float(frame%a.columns)/a.columns,float(frame/a.columns)/a.rows,float(frame%a.columns+1)/a.columns,float(frame/a.columns+1)/a.rows};});}
void integrate(void*,const Context* c) {c->each<Transform2D,const Velocity>([&](Entity,Transform2D& p,const Velocity& v){p.x+=v.x*c->frame->dt;p.y+=v.y*c->frame->dt;});}
std::shared_ptr<GameRegistry>& current(){static auto value=std::make_shared<GameRegistry>();return value;}
void check(bool test,const std::string& message){if(!test)throw std::runtime_error(message);}
bool validName(const char* s){return s&&*s&&std::strlen(s)<=128&&std::all_of(s,s+std::strlen(s),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='.';});}
}
GameRegistry::GameRegistry(){
    for(auto d:{sdk::component<Transform2D>("Transform 2D",transformFields,2),sdk::component<Velocity>("Velocity",velocityFields,2,transformDependency,1),sdk::component<SpriteRenderer>("Sprite Renderer",spriteFields,5,transformDependency,1)})components_.push_back({d,sdk::id(d.name),true});
    for(auto d:{sdk::component<Pose2D>("Pose 2D",poseFields,3,transformDependency,1),sdk::component<Camera2D>("Camera 2D",cameraFields,3,transformDependency,1),sdk::component<SpriteRegion>("Sprite Region",regionFields,4),sdk::component<Collider2D>("Collider 2D (Trigger)",colliderFields,7,transformDependency,1),sdk::component<SpriteAnimation>("Sprite Animation",animationFields,7,animationDependencies,2,validAnimation)})components_.push_back({d,sdk::id(d.name),true});
    for(auto d:{sdk::component<Transform3D>("Transform 3D",transform3Fields,9),sdk::component<MeshRenderer>("Mesh Renderer",meshFields,3,transform3Dependency,1),sdk::component<Camera3D>("Camera 3D",camera3Fields,5,transform3Dependency,1,validCamera3),sdk::component<DirectionalLight>("Directional Light",lightFields,7,nullptr,0,validLight),sdk::component<PostProcess>("Post Processing",postFields,uint32_t(std::size(postFields)))})components_.push_back({d,sdk::id(d.name),true});
    for(auto d:{sdk::component<ShadowSettings>("Shadows",shadowFields,uint32_t(std::size(shadowFields)),nullptr,0,validShadows),sdk::component<EnvironmentLight>("Environment Light",environmentFields,uint32_t(std::size(environmentFields))),sdk::component<IndirectLight>("Indirect Light (RSM)",indirectFields,uint32_t(std::size(indirectFields)),nullptr,0,validIndirect)})components_.push_back({d,sdk::id(d.name),true});
    auto quality =
        sdk::component<RenderQuality>("Render Quality", qualityFields, uint32_t(std::size(qualityFields)));
    components_.push_back({quality, sdk::id(quality.name),true});
    sdk::System movement{"gm.IntegrateVelocity","速度积分",sdk::Phase::Simulation,nullptr,0,nullptr,nullptr,integrate};systems_.push_back({movement,sdk::id(movement.name),true});sdk::System animation{"gm.SpriteAnimation","精灵动画",sdk::Phase::Late,nullptr,0,nullptr,nullptr,animate};systems_.push_back({animation,sdk::id(animation.name),true});finalize();
}
GameRegistry::~GameRegistry(){
    if(library_){
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(library_));
#else
        dlclose(library_);
#endif
    }
    if(!shadow_.empty()){std::error_code ec;std::filesystem::remove(shadow_,ec);}
}
void GameRegistry::load(const std::filesystem::path& path){
    check(!library_&&!registered_,"A registry owns exactly one project module");original_=std::filesystem::canonical(path);
    shadow_=std::filesystem::temp_directory_path()/("gm-module-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));shadow_+=path.extension();
    std::filesystem::copy_file(original_,shadow_);
#ifdef _WIN32
    library_=LoadLibraryExW(shadow_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto entry=library_?reinterpret_cast<sdk::Entry>(GetProcAddress(static_cast<HMODULE>(library_),"gm_game_module")):nullptr;
#else
    library_=dlopen(shadow_.c_str(),RTLD_NOW|RTLD_LOCAL);auto entry=library_?reinterpret_cast<sdk::Entry>(dlsym(library_,"gm_game_module")):nullptr;
#endif
    check(entry!=nullptr,"Cannot load native game module / gm_game_module export: "+pathText(path));
#ifdef _WIN32
    auto extension=reinterpret_cast<sdk::ExtensionEntry>(GetProcAddress(static_cast<HMODULE>(library_),"gm_game_extensions"));
#else
    auto extension=reinterpret_cast<sdk::ExtensionEntry>(dlsym(library_,"gm_game_extensions"));
#endif
    loadStatic(entry,extension);
}
void GameRegistry::loadStatic(sdk::Entry entry,sdk::ExtensionEntry extension){
    check(entry&&!registered_,"Invalid or already registered static module");registered_=true;
    const auto* module=entry(sdk::AbiVersion);check(module&&module->abi==sdk::AbiVersion&&module->structSize==sizeof(sdk::Module),"Game module SDK ABI mismatch; rebuild the project");
    check(validName(module->name)&&module->componentCount<=256&&module->systemCount<=128,"Invalid game module descriptor");
    check(!module->componentCount||module->components,"Missing module components");check(!module->systemCount||module->systems,"Missing module systems");name_=module->name;
    for(uint32_t i=0;i<module->componentCount;++i){auto d=module->components[i];check(validName(d.name)&&!std::string(d.name).starts_with("gm."),"Invalid component name / reserved gm. namespace");components_.push_back({d,sdk::id(d.name),false});}
    for(uint32_t i=0;i<module->systemCount;++i){auto d=module->systems[i];check(validName(d.name),"Invalid System name");systems_.push_back({d,sdk::id(d.name),false});}
    finalize();
    if(extension)if(const auto* extra=extension(1,&hostServices())) {
        check(extra->version==1&&extra->structSize>=sizeof(sdk::ModuleExtensions)&&extra->stateCount<=128&&(!extra->stateCount||extra->states),"Invalid module extensions");
        std::set<std::string> names;
        for(uint32_t i=0;i<extra->stateCount;++i){auto* state=extra->states[i];check(state&&state->structSize>=sizeof(sdk::SystemState)&&validName(state->system)&&state->version&&state->save&&state->restore,"Invalid System state extension");const auto& sys=system(state->system);check(!sys.engine&&sys.descriptor.destroy&&names.insert(state->system).second,"Duplicate / invalid System state extension");stateHooks_.push_back(*state);}
    }
}
const sdk::SystemState* GameRegistry::stateHooks(const std::string& name) const {for(const auto& hook:stateHooks_)if(name==hook.system)return &hook;return nullptr;}
const NativeComponent& GameRegistry::component(sdk::TypeId id)const{for(const auto& d:components_)if(d.id==id)return d;throw std::runtime_error("Unknown component ID; load the correct project module");}
const NativeComponent& GameRegistry::component(const std::string& name)const{const auto& c=component(sdk::id(name.c_str()));check(name==c.descriptor.name,"Component ID collision");return c;}
const NativeSystem& GameRegistry::system(const std::string& name)const{for(const auto& s:systems_)if(name==s.descriptor.name)return s;throw std::runtime_error("Unknown System: "+name);}
std::string phaseName(sdk::Phase p){switch(p){case sdk::Phase::Input:return "Input";case sdk::Phase::Simulation:return "Simulation";case sdk::Phase::PostTransform:return "PostTransform";case sdk::Phase::Late:return "Late";}throw std::runtime_error("Invalid System phase");}
void GameRegistry::finalize(){
    std::set<sdk::TypeId> ids;for(const auto& c:components_){const auto& d=c.descriptor;
        check(ids.insert(c.id).second&&d.label&&d.version>0,"Duplicate / invalid component descriptor");check(d.size>0&&d.size<=4096&&d.alignment>0&&d.alignment<=alignof(std::max_align_t)&&(d.alignment&(d.alignment-1))==0&&d.size%d.alignment==0,"Unsupported component layout");
        check(d.fieldCount>0&&d.fieldCount<=128&&d.fields&&d.requiresCount<=256&&(!d.requiresCount||d.requiresTypes),"Invalid component fields/dependencies");
        std::set<std::string> fields;std::set<uint32_t> offsets;
        for(uint32_t i=0;i<d.fieldCount;++i){const auto& f=d.fields[i];check(validName(f.name)&&fields.insert(f.name).second&&f.label&&f.kind<=sdk::Kind::Texture&&f.offset%4==0&&f.offset+4<=d.size&&offsets.insert(f.offset).second,"Invalid / overlapping component field");check(std::isfinite(f.initial)&&std::isfinite(f.minimum)&&std::isfinite(f.maximum)&&f.minimum<=f.initial&&f.initial<=f.maximum,"Invalid component range/default");if(f.kind==sdk::Kind::Int)check(f.minimum>=INT32_MIN&&f.maximum<=INT32_MAX&&std::floor(f.initial)==f.initial,"Integer range exceeds int32");if(f.kind==sdk::Kind::Bool)check(f.minimum==0&&f.maximum==1&&(f.initial==0||f.initial==1),"Bool fields use uint32_t 0/1");}
    }
    std::set<sdk::TypeId> visiting,done;std::function<void(const NativeComponent&)> visit=[&](const NativeComponent& c){if(done.count(c.id))return;check(visiting.insert(c.id).second,"Component dependency cycle");for(uint32_t i=0;i<c.descriptor.requiresCount;++i)visit(component(c.descriptor.requiresTypes[i]));visiting.erase(c.id);done.insert(c.id);};for(const auto& c:components_)visit(c);
    ids.clear();for(const auto& s:systems_){check(ids.insert(s.id).second&&s.descriptor.label&&s.descriptor.update&&s.descriptor.phase<=sdk::Phase::Late&&s.descriptor.afterCount<=128&&(!s.descriptor.afterCount||s.descriptor.after),"Invalid / duplicate System");check(bool(s.descriptor.create)==bool(s.descriptor.destroy),"System create/destroy must be paired");check(s.descriptor.accessCount<=256&&(!s.descriptor.accessCount||s.descriptor.access),"Invalid System access contract");std::set<sdk::TypeId> access;for(uint32_t i=0;i<s.descriptor.accessCount;++i){auto term=s.descriptor.access[i];check(term.write<=1&&access.insert(term.type).second,"Duplicate / invalid System access term");if(term.type!=sdk::id(sdk::WorldPosition::gm_name)&&term.type!=sdk::id(sdk::WorldTransform3D::gm_name))component(term.type);else check(!term.write,"WorldPosition is computed, not writable");}}
    std::vector<NativeSystem> sorted;visiting.clear();done.clear();std::function<void(const NativeSystem&)> order=[&](const NativeSystem& s){if(done.count(s.id))return;check(visiting.insert(s.id).second,"System dependency cycle");for(uint32_t i=0;i<s.descriptor.afterCount;++i){const auto& dep=system(s.descriptor.after[i]);check(dep.descriptor.phase<=s.descriptor.phase,"System dependency points to a later phase");order(dep);}visiting.erase(s.id);done.insert(s.id);sorted.push_back(s);};
    for(uint32_t phase=0;phase<4;++phase)for(const auto& s:systems_)if(uint32_t(s.descriptor.phase)==phase)order(s);systems_=std::move(sorted);
    for(int generation:{0,1,2,3}){
    fingerprint_=sdk::id(name_.c_str());auto hash=[&](std::string value){for(unsigned char c:value){fingerprint_^=c;fingerprint_*=1099511628211ull;}fingerprint_^=0xff;fingerprint_*=1099511628211ull;};
    // Canonical field representation is independent of padding and native pointer width.
    for(const auto& c:components_){if(c.engine&&((generation < 3 && c.id == sdk::id(RenderQuality::gm_name)) ||
                 (generation==0&&renderComponent(c.id))||(generation<2&&lightingComponent(c.id))))continue;const auto& d=c.descriptor;hash(d.name);hash(std::to_string(d.version));for(uint32_t i=0;i<d.fieldCount;++i){auto& f=d.fields[i];hash(f.name);hash(std::to_string(uint32_t(f.kind)));hash(std::to_string(std::bit_cast<uint64_t>(f.initial)));hash(std::to_string(std::bit_cast<uint64_t>(f.minimum)));hash(std::to_string(std::bit_cast<uint64_t>(f.maximum)));}for(uint32_t i=0;i<d.requiresCount;++i)hash(d.requiresTypes[i]);}
    for(const auto& s:systems_){hash(s.descriptor.name);hash(std::to_string(uint32_t(s.descriptor.phase)));for(uint32_t i=0;i<s.descriptor.afterCount;++i)hash(s.descriptor.after[i]);}
    if(generation==0)legacyFingerprint_=fingerprint_;if(generation==1)renderFingerprint_=fingerprint_;if(generation==2)lightingFingerprint_=fingerprint_;}
}
void GameRegistry::decode(sdk::TypeId type,std::span<const uint32_t> fields,void* out)const{const auto& d=component(type).descriptor;check(fields.size()==d.fieldCount,"Component field count mismatch");std::memset(out,0,d.size);for(uint32_t i=0;i<d.fieldCount;++i)std::memcpy(static_cast<unsigned char*>(out)+d.fields[i].offset,&fields[i],4);}
std::vector<uint32_t> GameRegistry::encode(sdk::TypeId type,const void* data)const{const auto& d=component(type).descriptor;std::vector<uint32_t> fields(d.fieldCount);for(uint32_t i=0;i<d.fieldCount;++i)std::memcpy(&fields[i],static_cast<const unsigned char*>(data)+d.fields[i].offset,4);return fields;}
void GameRegistry::validate(sdk::TypeId type,std::span<const uint32_t> fields)const{
    const auto& d=component(type).descriptor;check(fields.size()==d.fieldCount,"Component field count mismatch");for(uint32_t i=0;i<d.fieldCount;++i){auto& f=d.fields[i];double v=f.kind==sdk::Kind::Float?double(std::bit_cast<float>(fields[i])):f.kind==sdk::Kind::Int?double(std::bit_cast<int32_t>(fields[i])):double(fields[i]);check(std::isfinite(v)&&v>=f.minimum&&v<=f.maximum,std::string(d.name)+"."+f.name+" out of range");}
    if(d.validate){std::vector<std::max_align_t> storage((d.size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));decode(type,fields,storage.data());char error[512]{};const bool valid=d.validate(storage.data(),error,sizeof(error));error[sizeof(error)-1]=0;check(valid,std::string(d.name)+": "+error);}
}
std::shared_ptr<GameRegistry> gameRegistry(){return current();}
void useGameRegistry(std::shared_ptr<GameRegistry> registry){check(bool(registry),"Missing registry");current()=std::move(registry);}
}
