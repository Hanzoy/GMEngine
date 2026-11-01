#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// Native module boundary: no STL ownership, JSON, engine classes, or Flecs types.
// All descriptors and strings remain owned by the module until it is unloaded.
namespace gm::sdk {
inline constexpr uint32_t AbiVersion = 10;
using TypeId = uint64_t;
using Entity = uint64_t;
constexpr TypeId id(const char *text) {
    TypeId h = 14695981039346656037ull;
    while (*text) {
        h ^= uint8_t(*text++);
        h *= 1099511628211ull;
    }
    return h;
}
enum class Kind : uint32_t { Float, Int, Bool, Color, Image, Mesh, Material, Texture };
inline constexpr bool resourceKind(Kind kind){return kind==Kind::Image||kind==Kind::Mesh||kind==Kind::Material||kind==Kind::Texture;}
enum class Phase : uint32_t { Input, Simulation, PostTransform, Late };
struct Field {
    const char *name;
    const char *label;
    Kind kind;
    uint32_t offset;
    double initial, minimum, maximum;
};
struct Component {
    const char *name;
    const char *label;
    uint32_t version, size, alignment;
    const Field *fields;
    uint32_t fieldCount;
    const char *const *requiresTypes;
    uint32_t requiresCount;
    // Optional project rule, used by both authoring and binary loading. No exceptions.
    bool (*validate)(const void *data, char *error, uint32_t capacity);
};
struct Term {
    TypeId type;
    uint32_t write;
};
struct Batch {
    uint32_t count;
    const Entity *entities;
    void *const *columns;
    const uint32_t *strides;
};
// Logical scene coordinates (letterboxing removed). Button bits: left=1, right=2, middle=4.
// Canonical key codes retain Windows virtual-key numeric values on every platform.
// Edges and wheel are delivered once, including when a render frame has no simulation step.
struct Touch {int32_t id=-1;float x=0,y=0;uint32_t down=0,pressed=0,released=0;};
struct Gamepad {uint32_t connected=0,buttons=0;float axes[6]{};};
struct Input {
    float mouseX = -1, mouseY = -1, wheel = 0;
    uint32_t buttons = 0, pressed = 0, released = 0, focused = 0;
    uint32_t keys[8]{}, keyPress[8]{};
    uint32_t touchCount=0;
    Touch touches[10]{};
    // Axes: left X/Y, right X/Y, left/right trigger. Y grows down; triggers 0..1.
    // Buttons use XInput bit values: A=0x1000, B=0x2000, Start=0x10, D-pad=1/2/4/8.
    Gamepad gamepads[4]{};
    bool held(uint32_t key) const { return key < 256 && (keys[key / 32] & (1u << (key % 32))); }
    bool hit(uint32_t key) const { return key < 256 && (keyPress[key / 32] & (1u << (key % 32))); }
};
enum class CanvasKind : uint32_t { Rect, Border, Line, Text, Clip, ResetClip };
// Transient design-space drawing. Commands/text are copied by the host, cleared each tick,
// and replayed above sprites, below the authored HUD. No GPU pointers cross the ABI.
struct CanvasCommand {
    CanvasKind kind = CanvasKind::Rect;
    float x = 0, y = 0, w = 0, h = 0, size = 1;
    uint32_t color = 0xffffffff;
    char text[256]{};
};
struct Frame {
    float dt, inputX, inputY, width, height;
    uint64_t tick;
    Input input{};
};
// Structural writes are copied and committed after the current System returns.
// Component pointers are only valid until that boundary. Events are visible next tick.
struct Event {
    TypeId type;
    Entity source, target;
    uint32_t size;
    unsigned char data[64];
};
struct Api {
    uint32_t abi;
    void (*each)(void *world, const Term *terms, uint32_t count, void *user,
                 void (*visit)(void *, const Batch *));
    const void *(*get)(void *world, Entity entity, TypeId type);
    void *(*getMut)(void *world, Entity entity, TypeId type);
    Entity (*create)(void *world, Entity parent);
    void (*destroy)(void *world, Entity entity);
    void (*set)(void *world, Entity entity, TypeId type, const void *data);
    void (*remove)(void *world, Entity entity, TypeId type);
    bool (*alive)(void *world, Entity entity);
    Entity (*find)(void *world, const char *stableId);
    void (*emit)(void *world, const Event *event);
    void (*events)(void *world, TypeId type, void *user, void (*visit)(void *, const Event *));
    void (*switchScene)(void *world, const char *id);
    void (*draw)(void *world, const CanvasCommand *command);
    // Versioned, World-scoped services (UI, prefabs, platform input). No engine classes.
    const void *(*service)(void *world, TypeId type, uint32_t version);
};
struct Context {
    void *world;
    const Api *api;
    const Frame *frame;
    template <class T> const T *get(Entity entity) const {
        return static_cast<const T *>(api->get(world, entity, id(T::gm_name)));
    }
    template <class T> T *getMut(Entity entity) const {
        return static_cast<T *>(api->getMut(world, entity, id(T::gm_name)));
    }
    Entity create(Entity parent = 0) const {
        return api->create(world, parent);
    }
    void destroy(Entity entity) const {
        api->destroy(world, entity);
    }
    bool alive(Entity entity) const {
        return api->alive(world, entity);
    }
    Entity find(const char *stableId) const {
        return api->find(world, stableId);
    }
    template <class T> void set(Entity entity, const T &value) const {
        api->set(world, entity, id(T::gm_name), &value);
    }
    template <class T> void remove(Entity entity) const {
        api->remove(world, entity, id(T::gm_name));
    }
    void emit(const Event &event) const {
        api->emit(world, &event);
    }
    void switchScene(const char *id) const {
        api->switchScene(world, id);
    }
    void draw(const CanvasCommand &command) const { api->draw(world, &command); }
    template<class T> const T *service() const {
        if(!api->service)return nullptr;
        const auto *s=static_cast<const T*>(api->service(world,T::Id,T::Version));
        return s && s->structSize>=sizeof(T) && s->version==T::Version?s:nullptr;
    }
    template <class Fn> void events(TypeId type, Fn &&fn) const {
        using F = std::remove_reference_t<Fn>;
        api->events(world, type, &fn,
                    [](void *user, const Event *event) { (*static_cast<F *>(user))(*event); });
    }
    template <class... T, class Fn> void each(Fn &&fn) const {
        Term terms[] = {{id(std::remove_const_t<T>::gm_name), std::is_const_v<T> ? 0u : 1u}...};
        using F = std::remove_reference_t<Fn>;
        api->each(world, terms, sizeof...(T), &fn, [](void *user, const Batch *batch) {
            auto &f = *static_cast<F *>(user);
            for (uint32_t row = 0; row < batch->count; ++row)
                [&]<size_t... I>(std::index_sequence<I...>) {
                    f(batch->entities[row],
                      *reinterpret_cast<T *>(static_cast<unsigned char *>(batch->columns[I]) +
                                             row * batch->strides[I])...);
                }(std::index_sequence_for<T...>{});
        });
    }
};
struct System {
    const char *name;
    const char *label;
    Phase phase;
    const char *const *after;
    uint32_t afterCount;
    void *(*create)(const Context *);
    void (*destroy)(void *);
    void (*update)(void *, const Context *);
    // Optional access contract. When present, undeclared reads/writes are rejected.
    // A zero count retains unrestricted access for existing modules; no parallelism.
    const Term *access = nullptr;
    uint32_t accessCount = 0;
};
struct Module {
    uint32_t abi, structSize;
    const char *name;
    const Component *components;
    uint32_t componentCount;
    const System *systems;
    uint32_t systemCount;
};
using Entry = const Module *(*)(uint32_t hostAbi);
template <class T>
constexpr Component component(const char *label, const Field *fields, uint32_t count,
                              const char *const *dependencies = nullptr, uint32_t dependencyCount = 0,
                              bool (*validate)(const void *, char *, uint32_t) = nullptr,
                              uint32_t version = 1) {
    static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>,
                  "Components are flat value data; use handles for external resources, not owning pointers "
                  "or inheritance");
    static_assert(alignof(T) <= alignof(std::max_align_t));
    return {T::gm_name, label, version,      sizeof(T),       alignof(T),
            fields,     count, dependencies, dependencyCount, validate};
}
struct Transform2D {
    static constexpr const char *gm_name = "Transform2D";
    float x = 0, y = 0;
};
struct Velocity {
    static constexpr const char *gm_name = "Velocity";
    float x = 0, y = 0;
};
struct SpriteRenderer {
    static constexpr const char *gm_name = "SpriteRenderer";
    float width = 64, height = 64;
    uint32_t image = UINT32_MAX, tint = 0xffffffff;
    float order = 0;
};
struct WorldPosition {
    static constexpr const char *gm_name = "gm.WorldPosition";
    float x = 0, y = 0, a = 1, b = 0, c = 0, d = 1;
};
struct Pose2D {
    static constexpr const char *gm_name = "Pose2D";
    float rotation = 0, scaleX = 1, scaleY = 1;
};
struct Camera2D {
    static constexpr const char *gm_name = "Camera2D";
    float zoom = 1;
    uint32_t enabled = 1;
    int32_t priority = 0;
};
struct SpriteRegion {
    static constexpr const char *gm_name = "SpriteRegion";
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
struct SpriteAnimation {
    static constexpr const char *gm_name = "SpriteAnimation";
    int32_t columns = 1, rows = 1, frames = 1;
    float fps = 12, time = 0;
    uint32_t playing = 1, loop = 1;
};
struct Collider2D {
    static constexpr const char *gm_name = "Collider2D";
    float width = 64, height = 64, offsetX = 0, offsetY = 0;
    int32_t layer = 1, mask = 65535;
    uint32_t enabled = 1;
};
inline constexpr TypeId CollisionEnter = id("gm.CollisionEnter"), CollisionStay = id("gm.CollisionStay"),
                        CollisionExit = id("gm.CollisionExit");
} // namespace gm::sdk
#define GM_FLOAT(T, F, L, D, MIN, MAX) {#F, L, gm::sdk::Kind::Float, uint32_t(offsetof(T, F)), D, MIN, MAX}
#define GM_INT(T, F, L, D, MIN, MAX) {#F, L, gm::sdk::Kind::Int, uint32_t(offsetof(T, F)), D, MIN, MAX}
#define GM_BOOL(T, F, L, D) {#F, L, gm::sdk::Kind::Bool, uint32_t(offsetof(T, F)), D, 0, 1}
#if defined(_WIN32)
#define GM_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define GM_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif
