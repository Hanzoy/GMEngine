#pragma once
#include "extensions.hpp"
namespace gm::sdk {
enum class ResourceStatus : uint32_t { Pending, Ready, Failed };
struct ResourceData {
    ResourceStatus status{};
    const void *data = nullptr;
    uint32_t size = 0;
    char error[512]{};
};
struct ResourceService {
    static constexpr TypeId Id = id("gm.Resources");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    void *(*open)(const char *root, Error *);
    void (*close)(void *);
    // Package-relative files only, no path escapes. At most 64 handles per session.
    // Reads run on workers. Unchanged files are shared by concurrently retained handles.
    uint64_t (*request)(void *, const char *relativeFile, Error *);
    bool (*poll)(void *, uint64_t, ResourceData *, Error *);
    // Borrowed data remains valid until release/close. Last release unloads/cancels it.
    bool (*release)(void *, uint64_t, Error *);
};
struct PrefabService {
    static constexpr TypeId Id = id("gm.Prefabs");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    // World scoped: compiled .gmprefab bytes, same module/schema, one root, no HUD bindings.
    uint64_t (*load)(void *world, const void *, uint32_t, Error *);
    Entity (*instantiate)(void *world, uint64_t prefab, Entity parent, Error *);
    bool (*unload)(void *world, uint64_t, Error *);
};
struct ContentService {
    static constexpr TypeId Id = id("gm.Content");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    // UTF-8 absolute build/package directory, valid for this World's lifetime.
    const char *(*root)(void *world);
};
} // namespace gm::sdk
