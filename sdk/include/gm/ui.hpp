#pragma once
#include "extensions.hpp"
namespace gm::sdk {
// Authored event name becomes Event::type. Data is UiEventData; delivered next tick.
struct UiEventData {
    TypeId node = 0;
    int32_t row = -1;
    uint32_t reserved = 0;
};
struct UiService {
    static constexpr TypeId Id = id("gm.UI");
    static constexpr uint32_t Version = 1;
    uint32_t structSize, version;
    // Text binds to all nodes with bindText=name, or to a node's id when no binding exists.
    bool (*text)(void *world, const char *name, const char *utf8, Error *);
    bool (*visible)(void *world, const char *node, bool, Error *);
    // Virtualized list: copied strings, at most 10,000 rows / 2 MiB. Only visible rows draw.
    bool (*list)(void *world, const char *node, const char *const *rows, uint32_t count, Error *);
    bool (*scroll)(void *world, const char *node, float pixels, Error *);
};
} // namespace gm::sdk
