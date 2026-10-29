#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// Native module boundary: no STL ownership, JSON, engine classes, or Flecs types.
// All descriptors and strings remain owned by the module until it is unloaded.
namespace gm::sdk {
inline constexpr uint32_t AbiVersion=7;
using TypeId=uint64_t;
using Entity=uint64_t;
constexpr TypeId id(const char* text) {TypeId h=14695981039346656037ull;while(*text){h^=uint8_t(*text++);h*=1099511628211ull;}return h;}
enum class Kind:uint32_t {Float,Int,Bool,Color,Image};
enum class Phase:uint32_t {Input,Simulation,PostTransform,Late};
struct Field {const char* name;const char* label;Kind kind;uint32_t offset;double initial,minimum,maximum;};
struct Component {
    const char* name;const char* label;uint32_t version,size,alignment;
    const Field* fields;uint32_t fieldCount;
    const char* const* requiresTypes;uint32_t requiresCount;
    // Optional project rule, used by both authoring and binary loading. No exceptions.
    bool (*validate)(const void* data,char* error,uint32_t capacity);
};
struct Term {TypeId type;uint32_t write;};
struct Batch {uint32_t count;const Entity* entities;void* const* columns;const uint32_t* strides;};
struct Frame {float dt,inputX,inputY,width,height;uint64_t tick;};
struct Api {
    uint32_t abi;
    void (*each)(void* world,const Term* terms,uint32_t count,void* user,void (*visit)(void*,const Batch*));
    const void* (*get)(void* world,Entity entity,TypeId type);
    void* (*getMut)(void* world,Entity entity,TypeId type);
};
struct Context {
    void* world;const Api* api;const Frame* frame;
    template<class T> const T* get(Entity entity) const {return static_cast<const T*>(api->get(world,entity,id(T::gm_name)));}
    template<class T> T* getMut(Entity entity) const {return static_cast<T*>(api->getMut(world,entity,id(T::gm_name)));}
    template<class... T,class Fn> void each(Fn&& fn) const {
        Term terms[]={{id(std::remove_const_t<T>::gm_name),std::is_const_v<T>?0u:1u}...};
        using F=std::remove_reference_t<Fn>;
        api->each(world,terms,sizeof...(T),&fn,[](void* user,const Batch* batch){
            auto& f=*static_cast<F*>(user);
            for(uint32_t row=0;row<batch->count;++row)
                [&]<size_t... I>(std::index_sequence<I...>){f(batch->entities[row],*reinterpret_cast<T*>(static_cast<unsigned char*>(batch->columns[I])+row*batch->strides[I])...);}(std::index_sequence_for<T...>{});
        });
    }
};
struct System {
    const char* name;const char* label;Phase phase;
    const char* const* after;uint32_t afterCount;
    void* (*create)(const Context*);void (*destroy)(void*);
    void (*update)(void*,const Context*);
};
struct Module {uint32_t abi,structSize;const char* name;const Component* components;uint32_t componentCount;const System* systems;uint32_t systemCount;};
using Entry=const Module* (*)(uint32_t hostAbi);
template<class T> constexpr Component component(const char* label,const Field* fields,uint32_t count,const char* const* dependencies=nullptr,uint32_t dependencyCount=0,bool (*validate)(const void*,char*,uint32_t)=nullptr,uint32_t version=1) {
    static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>,"Components are flat value data; use handles for external resources, not owning pointers or inheritance");
    static_assert(alignof(T)<=alignof(std::max_align_t));
    return {T::gm_name,label,version,sizeof(T),alignof(T),fields,count,dependencies,dependencyCount,validate};
}
struct Transform2D {static constexpr const char* gm_name="Transform2D";float x=0,y=0;};
struct Velocity {static constexpr const char* gm_name="Velocity";float x=0,y=0;};
struct SpriteRenderer {static constexpr const char* gm_name="SpriteRenderer";float width=64,height=64;uint32_t image=UINT32_MAX,tint=0xffffffff;float order=0;};
struct WorldPosition {static constexpr const char* gm_name="gm.WorldPosition";float x=0,y=0;};
}
#define GM_FLOAT(T,F,L,D,MIN,MAX) {#F,L,gm::sdk::Kind::Float,uint32_t(offsetof(T,F)),D,MIN,MAX}
#define GM_INT(T,F,L,D,MIN,MAX) {#F,L,gm::sdk::Kind::Int,uint32_t(offsetof(T,F)),D,MIN,MAX}
#define GM_BOOL(T,F,L,D) {#F,L,gm::sdk::Kind::Bool,uint32_t(offsetof(T,F)),D,0,1}
#if defined(_WIN32)
#define GM_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define GM_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif
