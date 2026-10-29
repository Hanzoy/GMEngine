#pragma once
#include "runtime_document.hpp"
#include "game_registry.hpp"
#include <memory>
namespace gm {
using Transform2D=sdk::Transform2D;
using Velocity=sdk::Velocity;
using SpriteRenderer=sdk::SpriteRenderer;
struct ComponentValue {sdk::TypeId type=0;uint32_t version=1;std::vector<uint32_t> fields;};
struct SceneEntity {std::string id;uint32_t parent=NoIndex;std::vector<ComponentValue> components;};
enum class BindingKind:uint32_t {NumberText=1,NumberBar=2,NumberChange=3};
struct SceneBinding {uint32_t entity=0,node=0;BindingKind kind=BindingKind::NumberText;float amount=0;sdk::TypeId component=0;uint32_t field=0,maximum=NoIndex;};
struct CompiledScene {
    float width=1280,height=720;uint32_t background=0xff2d2118;
    uint64_t schema=0;std::string module;std::vector<std::string> disabledSystems;
    std::vector<SceneEntity> entities;CompiledUi imageBank,hud;std::vector<SceneBinding> bindings;
};
const ComponentValue* findValue(const SceneEntity& entity,sdk::TypeId type);
void validateSceneBinary(const CompiledScene& scene);
std::vector<uint8_t> encodeScene(const CompiledScene& scene);
CompiledScene decodeScene(std::span<const uint8_t> bytes);
CompiledScene loadScene(const std::filesystem::path& path);
void saveScene(const std::filesystem::path& path,const CompiledScene& scene);
struct SceneInput {float x=0,y=0;};
struct SceneSprite {uint32_t entity;Rect box;uint32_t image,tint;float order;};
class SceneRuntime {
public:
    explicit SceneRuntime(CompiledScene scene);
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime&)=delete;
    SceneRuntime& operator=(const SceneRuntime&)=delete;
    void tick(float dt,SceneInput input);
    void reset();
    bool activateUi(uint32_t node);
    Transform2D position(uint32_t entity) const;
    std::vector<uint32_t> component(uint32_t entity,sdk::TypeId type) const;
    uint32_t find(const std::string& stableId) const;
    uint64_t ticks() const;
    const CompiledScene& asset() const {return asset_;}
    const std::vector<SceneSprite>& sprites() const {return sprites_;}
    RuntimeDocument& hud() {return *hud_;}
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    std::shared_ptr<GameRegistry> registry_;
    CompiledScene asset_;std::unique_ptr<RuntimeDocument> hud_;
    std::vector<SceneSprite> sprites_;std::vector<uint32_t> spriteRows_;
    struct BoundValue {double value=-1,maximum=-1;};std::vector<BoundValue> bound_;
    void extract();void syncUi();
};
}
