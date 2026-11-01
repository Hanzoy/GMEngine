#pragma once
#include <gm/extensions.hpp>
#include <gm/render.hpp>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace gm {
struct NativeComponent {sdk::Component descriptor{};sdk::TypeId id=0;bool engine=false;};
struct NativeSystem {sdk::System descriptor{};sdk::TypeId id=0;bool engine=false;};
class GameRegistry {
public:
    GameRegistry();
    ~GameRegistry();
    GameRegistry(const GameRegistry&)=delete;
    void load(const std::filesystem::path& module);
    void loadStatic(sdk::Entry entry,sdk::ExtensionEntry extension=nullptr);
    const NativeComponent& component(sdk::TypeId id) const;
    const NativeComponent& component(const std::string& name) const;
    const NativeSystem& system(const std::string& name) const;
    const auto& components() const {return components_;}
    const auto& systems() const {return systems_;}
    const sdk::SystemState* stateHooks(const std::string& name) const;
    uint64_t fingerprint() const {return fingerprint_;}
    bool compatible(uint64_t schema) const {return schema==fingerprint_||schema==legacyFingerprint_||schema==renderFingerprint_ ||
               schema == lightingFingerprint_;}
    const std::string& name() const {return name_;}
    const std::filesystem::path& modulePath() const {return original_;}
    void decode(sdk::TypeId type,std::span<const uint32_t> fields,void* output) const;
    std::vector<uint32_t> encode(sdk::TypeId type,const void* data) const;
    void validate(sdk::TypeId type,std::span<const uint32_t> fields) const;
private:
    void finalize();
    bool registered_=false;
    std::vector<sdk::SystemState> stateHooks_;
    std::vector<NativeComponent> components_;std::vector<NativeSystem> systems_;
    std::string name_="GMEngine";uint64_t fingerprint_=0,legacyFingerprint_=0,renderFingerprint_=0, lightingFingerprint_ = 0;void* library_=nullptr;
    std::filesystem::path original_,shadow_;
};
std::shared_ptr<GameRegistry> gameRegistry();
void useGameRegistry(std::shared_ptr<GameRegistry> registry);
std::string phaseName(sdk::Phase phase);
}
