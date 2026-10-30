#pragma once
#include "game.hpp"

namespace gm::sdk {
// Optional, versioned interfaces. Check service availability before use.
// Export gm_game_extensions(1, host) to opt in. Old hosts ignore it; check service availability.
struct Error {
    char message[512]{};
};
struct ByteSink {
    void *user;
    bool (*write)(void *, const void *, uint32_t);
};
struct StorageService {
    static constexpr TypeId Id = id("gm.Storage");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    // One exclusive writer lease per app/profile, held until close. No exceptions across these calls.
    void *(*open)(const char *app, const char *profile, Error *);
    void (*close)(void *);
    bool (*read)(void *, const char *slot, const ByteSink *, Error *);
    bool (*write)(void *, const char *slot, const void *data, uint32_t size, Error *);
    bool (*exists)(void *, const char *slot);
};
struct HostServices {
    uint32_t structSize, version;
    const void *(*query)(TypeId service, uint32_t version);
    template <class T> const T *service() const {
        if (structSize < sizeof(HostServices) || version != 1 || !query)
            return nullptr;
        auto result = static_cast<const T *>(query(T::Id, T::Version));
        return result && result->structSize >= sizeof(T) && result->version == T::Version ? result : nullptr;
    }
};
struct SystemState {
    uint32_t structSize;
    const char *system;
    uint32_t version;
    // Runs on the owner thread at a tick boundary. State owns no engine pointers on disk.
    bool (*save)(const void *, const ByteSink *, Error *);
    // Builds independent state without external side effects. Version migration belongs here.
    // Return nullptr on failure; original World and all original System states remain untouched.
    void *(*restore)(const Context *, uint32_t savedVersion, const void *data, uint32_t size, Error *);
};
struct ModuleExtensions {
    uint32_t structSize, version;
    const SystemState *const *states;
    uint32_t stateCount;
};
using ExtensionEntry = const ModuleExtensions *(*)(uint32_t version, const HostServices *);
} // namespace gm::sdk
