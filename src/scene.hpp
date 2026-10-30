#pragma once
#include "runtime_document.hpp"
#include "game_registry.hpp"
#include "fixed_step.hpp"
#include "storage.hpp"
#include <memory>
namespace gm {
using Transform2D = sdk::Transform2D;
using Velocity = sdk::Velocity;
using SpriteRenderer = sdk::SpriteRenderer;
struct ComponentValue {
    sdk::TypeId type = 0;
    uint32_t version = 1;
    std::vector<uint32_t> fields;
};
struct SceneEntity {
    std::string id;
    uint32_t parent = NoIndex;
    std::vector<ComponentValue> components;
};
enum class BindingKind : uint32_t { NumberText = 1, NumberBar = 2, NumberChange = 3 };
struct SceneBinding {
    uint32_t entity = 0, node = 0;
    BindingKind kind = BindingKind::NumberText;
    float amount = 0;
    sdk::TypeId component = 0;
    uint32_t field = 0, maximum = NoIndex;
};
struct CompiledScene {
    float width = 1280, height = 720;
    uint32_t background = 0xff2d2118;
    uint64_t schema = 0;
    std::string module;
    std::vector<std::string> disabledSystems;
    std::vector<SceneEntity> entities;
    CompiledUi imageBank, hud;
    std::vector<SceneBinding> bindings;
};
const ComponentValue *findValue(const SceneEntity &entity, sdk::TypeId type);
void validateSceneBinary(const CompiledScene &scene);
std::vector<uint8_t> encodeScene(const CompiledScene &scene);
CompiledScene decodeScene(std::span<const uint8_t> bytes);
CompiledScene loadScene(const std::filesystem::path &path);
void saveScene(const std::filesystem::path &path, const CompiledScene &scene);
struct SceneInput {
    float x = 0, y = 0;
    sdk::Input pointer{};
};
struct SceneSprite {
    uint32_t entity;
    Rect box;
    uint32_t image, tint;
    float order;
    sdk::WorldPosition transform{};
    sdk::SpriteRegion region{};
};
struct SceneCamera {
    sdk::WorldPosition transform;
    float zoom = 1;
};
struct RayHit {
    sdk::Entity entity = 0;
    float distance = 0, x = 0, y = 0;
};
struct SystemTiming {
    std::string name;
    double milliseconds = 0, maximum = 0;
    uint64_t calls = 0;
};
struct RuntimeStats {
    uint32_t entities = 0, queries = 0, pendingCommands = 0;
    uint64_t componentBytes = 0;
    double simulationMs = 0, droppedSeconds = 0;
    std::vector<SystemTiming> systems;
};
class SceneRuntime {
  public:
    explicit SceneRuntime(CompiledScene scene);
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime &) = delete;
    SceneRuntime &operator=(const SceneRuntime &) = delete;
    void tick(float dt, SceneInput input);
    uint32_t advance(double elapsed, SceneInput input);
    void resetClock() {
        clock_.reset();
    }
    sdk::Entity create(sdk::Entity parent = 0);
    void destroy(sdk::Entity entity);
    void set(sdk::Entity entity, sdk::TypeId type, std::span<const uint32_t> fields);
    void remove(sdk::Entity entity, sdk::TypeId type);
    sdk::Entity instantiate(uint32_t source, sdk::Entity parent = 0);
    bool alive(uint32_t index) const;
    sdk::Entity entity(uint32_t index) const;
    const std::string& entityId(sdk::Entity entity) const;
    const std::vector<SceneEntity> &entities() const;
    RuntimeStats stats() const;
    CompiledScene snapshot() const;
    void restore(CompiledScene scene);
    std::vector<uint8_t> checkpoint() const;
    void restoreCheckpoint(std::span<const uint8_t> bytes);
    std::filesystem::path saveSlot(const std::string& slot);
    std::filesystem::path loadSlot(const std::string& slot);
    std::string takeSceneRequest();
    SceneCamera camera() const;
    std::vector<sdk::Entity> queryArea(Rect area, uint32_t mask = 65535) const;
    RayHit raycast(float x, float y, float dx, float dy, float length, uint32_t mask = 65535) const;
    void reset();
    bool activateUi(uint32_t node);
    Transform2D position(uint32_t entity) const;
    std::vector<uint32_t> component(uint32_t entity, sdk::TypeId type) const;
    uint32_t find(const std::string &stableId) const;
    uint64_t ticks() const;
    const CompiledScene &asset() const {
        return asset_;
    }
    const std::vector<SceneSprite> &sprites() const {
        return sprites_;
    }
    const std::vector<sdk::CanvasCommand> &canvas() const;
    RuntimeDocument &hud() {
        return *hud_;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<GameRegistry> registry_;
    std::unique_ptr<StorageSession> saveStorage_;
    CompiledScene asset_;
    std::unique_ptr<RuntimeDocument> hud_;
    std::vector<SceneSprite> sprites_;
    std::vector<uint32_t> spriteRows_;
    FixedStep clock_;
    SceneInput pendingInput_{};
    double simulationMs_ = 0;
    uint64_t spriteRevision_ = 0;
    struct BoundValue {
        double value = -1, maximum = -1;
    };
    std::vector<BoundValue> bound_;
    void extract();
    void syncUi();
};
} // namespace gm
