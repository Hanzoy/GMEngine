#include "scene.hpp"
#include <flecs.h>
#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
namespace gm {
struct SceneRuntime::Impl {
    std::shared_ptr<GameRegistry> registry;flecs::world world;
    struct Type {ecs_entity_t id;uint32_t size;};std::unordered_map<sdk::TypeId,Type> types;
    struct Query {ecs_query_t* value=nullptr;bool active=false;~Query(){if(value)ecs_query_fini(value);}};
    std::map<std::array<uint64_t,17>,std::unique_ptr<Query>> queries; struct EntityIndex {static constexpr const char* gm_name="gm.EntityIndex";uint32_t value;};
    struct RunningSystem {sdk::System descriptor;void* state=nullptr;};std::vector<RunningSystem> systems;
    std::vector<ecs_entity_t> entities;std::vector<uint32_t> order;std::unordered_map<std::string,uint32_t> ids;
    sdk::Frame frame{};
    static const sdk::Api& api(){static const sdk::Api value{sdk::AbiVersion,each,get,getMut};return value;}
    sdk::Context context(){return {this,&api(),&frame};}
    static const void* get(void* ptr,sdk::Entity e,sdk::TypeId type){auto& self=*static_cast<Impl*>(ptr);auto t=self.types.find(type);return t==self.types.end()||!ecs_is_alive(self.world.c_ptr(),e)?nullptr:ecs_get_id(self.world.c_ptr(),e,t->second.id);}
    static void* getMut(void* ptr,sdk::Entity e,sdk::TypeId type){auto& self=*static_cast<Impl*>(ptr);auto t=self.types.find(type);return t==self.types.end()||!ecs_is_alive(self.world.c_ptr(),e)||!ecs_has_id(self.world.c_ptr(),e,t->second.id)?nullptr:ecs_get_mut_id(self.world.c_ptr(),e,t->second.id);}
    static void each(void* ptr,const sdk::Term* terms,uint32_t count,void* user,void (*callback)(void*,const sdk::Batch*)){
        auto& self=*static_cast<Impl*>(ptr);if(!count||count>8||!terms||!callback)throw std::runtime_error("Invalid native query");std::array<uint64_t,17> key{};key[0]=count;for(uint32_t i=0;i<count;++i){key[1+i*2]=terms[i].type;key[2+i*2]=terms[i].write;}
        auto found=self.queries.find(key);if(found==self.queries.end()){ecs_query_desc_t desc{};desc.cache_kind=EcsQueryCacheAll;for(uint32_t i=0;i<count;++i){auto t=self.types.find(terms[i].type);if(t==self.types.end())throw std::runtime_error("System queried an unregistered component");desc.terms[i].id=t->second.id;desc.terms[i].inout=terms[i].write?EcsInOut:EcsIn;}auto q=std::make_unique<Query>();q->value=ecs_query_init(self.world.c_ptr(),&desc);if(!q->value)throw std::runtime_error("Cannot create native query");found=self.queries.emplace(std::move(key),std::move(q)).first;}
        auto& q=*found->second;if(q.active)throw std::runtime_error("Recursive iteration of the same query is unsupported");q.active=true;auto it=ecs_query_iter(self.world.c_ptr(),q.value);
        try{while(ecs_query_next(&it)){void* columns[8]{};uint32_t strides[8]{};for(uint32_t i=0;i<count;++i){strides[i]=self.types.at(terms[i].type).size;columns[i]=ecs_field_w_size(&it,strides[i],int8_t(i));}sdk::Batch batch{uint32_t(it.count),it.entities,columns,strides};callback(user,&batch);}q.active=false;}catch(...){ecs_iter_fini(&it);q.active=false;throw;}
    }
    explicit Impl(const CompiledScene& s,std::shared_ptr<GameRegistry> r):registry(std::move(r)){
        auto registerType=[&](sdk::TypeId key,uint32_t size,uint32_t alignment){ecs_component_desc_t d{};d.entity=world.entity().id();d.type.size=int32_t(size);d.type.alignment=int32_t(alignment);auto id=ecs_component_init(world.c_ptr(),&d);if(!id)throw std::runtime_error("Cannot register native component");types.emplace(key,Type{id,size});};
        for(const auto& c:registry->components())registerType(c.id,c.descriptor.size,c.descriptor.alignment);
        registerType(sdk::id(sdk::WorldPosition::gm_name),sizeof(sdk::WorldPosition),alignof(sdk::WorldPosition));registerType(sdk::id(EntityIndex::gm_name),sizeof(EntityIndex),alignof(EntityIndex));entities.reserve(s.entities.size());
        for(uint32_t i=0;i<s.entities.size();++i){auto e=world.entity().id();entities.push_back(e);ids[s.entities[i].id]=i;EntityIndex index{i};ecs_set_id(world.c_ptr(),e,types.at(sdk::id(EntityIndex::gm_name)).id,sizeof(index),&index);sdk::WorldPosition pos{};ecs_set_id(world.c_ptr(),e,types.at(sdk::id(sdk::WorldPosition::gm_name)).id,sizeof(pos),&pos);
            for(const auto& c:s.entities[i].components){const auto& d=registry->component(c.type).descriptor;std::vector<std::max_align_t> data((d.size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));registry->decode(c.type,c.fields,data.data());ecs_set_id(world.c_ptr(),e,types.at(c.type).id,d.size,data.data());}}
        std::vector<bool> visited(entities.size());std::function<void(uint32_t)> visit=[&](uint32_t i){if(visited[i])return;auto p=s.entities[i].parent;if(p!=NoIndex)visit(p);visited[i]=true;order.push_back(i);};for(uint32_t i=0;i<entities.size();++i)visit(i);frame.width=s.width;frame.height=s.height;transforms(s);
        try{auto c=context();for(const auto& sys:registry->systems()){const auto& d=sys.descriptor;if(std::find(s.disabledSystems.begin(),s.disabledSystems.end(),d.name)!=s.disabledSystems.end())continue;void* state=d.create?d.create(&c):nullptr;systems.push_back({d,state});}}catch(...){shutdown();throw;}
    }
    void shutdown(){for(auto it=systems.rbegin();it!=systems.rend();++it)if(it->descriptor.destroy)it->descriptor.destroy(it->state);systems.clear();queries.clear();}
    ~Impl(){shutdown();}
    void transforms(const CompiledScene& s){for(auto i:order){auto e=entities[i];auto* local=static_cast<const Transform2D*>(get(this,e,sdk::id(Transform2D::gm_name)));auto* pos=static_cast<sdk::WorldPosition*>(getMut(this,e,sdk::id(sdk::WorldPosition::gm_name)));*pos={local?local->x:0,local?local->y:0};auto p=s.entities[i].parent;if(p!=NoIndex){auto* parent=static_cast<const sdk::WorldPosition*>(get(this,entities[p],sdk::id(sdk::WorldPosition::gm_name)));pos->x+=parent->x;pos->y+=parent->y;}}}
    void phase(sdk::Phase phase){auto c=context();for(auto& s:systems)if(s.descriptor.phase==phase)s.descriptor.update(s.state,&c);}
    double number(uint32_t entity,sdk::TypeId type,uint32_t field){const auto& f=registry->component(type).descriptor.fields[field];auto* data=static_cast<const unsigned char*>(get(this,entities.at(entity),type));if(!data)throw std::runtime_error("Runtime component missing");uint32_t word;std::memcpy(&word,data+f.offset,4);return f.kind==sdk::Kind::Float?double(std::bit_cast<float>(word)):double(std::bit_cast<int32_t>(word));}
};
SceneRuntime::SceneRuntime(CompiledScene scene):registry_(gameRegistry()),asset_(std::move(scene)){validateSceneBinary(asset_);reset();}
SceneRuntime::~SceneRuntime()=default;
void SceneRuntime::reset(){auto next=std::make_unique<Impl>(asset_,registry_);auto hud=std::make_unique<RuntimeDocument>(asset_.hud);impl_=std::move(next);hud_=std::move(hud);sprites_.clear();spriteRows_.clear();bound_.assign(asset_.bindings.size(),BoundValue{});extract();syncUi();}
void SceneRuntime::tick(float dt,SceneInput input){if(!std::isfinite(dt)||dt<0||!std::isfinite(input.x)||!std::isfinite(input.y))throw std::runtime_error("Invalid simulation input");dt=std::min(dt,.1f);float n=std::hypot(input.x,input.y);if(n>1){input.x/=n;input.y/=n;}impl_->frame.dt=dt;impl_->frame.inputX=input.x;impl_->frame.inputY=input.y;++impl_->frame.tick;impl_->phase(sdk::Phase::Input);impl_->phase(sdk::Phase::Simulation);impl_->transforms(asset_);impl_->phase(sdk::Phase::PostTransform);impl_->phase(sdk::Phase::Late);extract();syncUi();}
void SceneRuntime::extract(){
    if(spriteRows_.empty())spriteRows_.assign(asset_.entities.size(),NoIndex);
    bool reorder=false;
    impl_->context().each<const Impl::EntityIndex,const sdk::WorldPosition,const SpriteRenderer>([&](sdk::Entity,const Impl::EntityIndex& index,const sdk::WorldPosition& p,const SpriteRenderer& s){
        auto& row=spriteRows_[index.value];
        if(row==NoIndex){row=uint32_t(sprites_.size());sprites_.push_back({});reorder=true;}
        auto& sprite=sprites_[row];reorder|=sprite.order!=s.order;sprite={index.value,{p.x,p.y,s.width,s.height},s.image,s.tint,s.order};
    });
    if(reorder){std::sort(sprites_.begin(),sprites_.end(),[](const auto& a,const auto& b){return a.order==b.order?a.entity<b.entity:a.order<b.order;});for(uint32_t i=0;i<sprites_.size();++i)spriteRows_[sprites_[i].entity]=i;}
}
void SceneRuntime::syncUi(){for(size_t i=0;i<asset_.bindings.size();++i){auto& b=asset_.bindings[i];if(b.kind==BindingKind::NumberChange)continue;auto value=impl_->number(b.entity,b.component,b.field),maximum=b.maximum==NoIndex?0:impl_->number(b.entity,b.component,b.maximum);auto& cached=bound_[i];if(value==cached.value&&maximum==cached.maximum)continue;if(b.kind==BindingKind::NumberText){if(std::ceil(value)!=std::ceil(cached.value)||std::ceil(maximum)!=std::ceil(cached.maximum))hud_->setBoundText(b.node,std::to_string(int64_t(std::ceil(value)))+(b.maximum==NoIndex?std::string():" / "+std::to_string(int64_t(std::ceil(maximum)))));}else hud_->setBoundWidth(b.node,asset_.hud.nodes[b.node].bounds.w*float(maximum>0?std::clamp(value/maximum,0.,1.):0));cached={value,maximum};}}
bool SceneRuntime::activateUi(uint32_t node){for(const auto& b:asset_.bindings)if(b.node==node&&b.kind==BindingKind::NumberChange){const auto& d=registry_->component(b.component).descriptor;const auto& f=d.fields[b.field];double hi=f.maximum;if(b.maximum!=NoIndex)hi=std::min(hi,impl_->number(b.entity,b.component,b.maximum));hi=std::max(hi,f.minimum);auto value=std::clamp(impl_->number(b.entity,b.component,b.field)+b.amount,f.minimum,hi);auto words=component(b.entity,b.component);words[b.field]=f.kind==sdk::Kind::Float?std::bit_cast<uint32_t>(float(value)):std::bit_cast<uint32_t>(int32_t(value));registry_->validate(b.component,words);auto* target=Impl::getMut(impl_.get(),impl_->entities[b.entity],b.component);registry_->decode(b.component,words,target);syncUi();return true;}return false;}
Transform2D SceneRuntime::position(uint32_t index)const{auto* p=static_cast<const sdk::WorldPosition*>(Impl::get(impl_.get(),impl_->entities.at(index),sdk::id(sdk::WorldPosition::gm_name)));return {p->x,p->y};}
std::vector<uint32_t> SceneRuntime::component(uint32_t index,sdk::TypeId type)const{auto* data=Impl::get(impl_.get(),impl_->entities.at(index),type);if(!data)throw std::runtime_error("Runtime component not found");return registry_->encode(type,data);}
uint32_t SceneRuntime::find(const std::string& id)const{auto it=impl_->ids.find(id);return it==impl_->ids.end()?NoIndex:it->second;}
uint64_t SceneRuntime::ticks()const{return impl_->frame.tick;}
}
