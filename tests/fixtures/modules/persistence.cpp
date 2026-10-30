#include <gm/extensions.hpp>
#include <algorithm>
#include <cstring>
#include <vector>
using namespace gm::sdk;
namespace {
struct Counter {
    static constexpr const char *gm_name = "test.Saved";
    int32_t total = 0;
};
struct Private {
    int32_t total = 0;
    std::vector<int32_t> cargo{3, 5, 9};
};
void *create(const Context *) {
    return new Private;
}
void destroy(void *ptr) {
    delete static_cast<Private *>(ptr);
}
void update(void *ptr, const Context *c) {
    auto &s = *static_cast<Private *>(ptr);
    c->events(id("test.CheckpointEvent"), [&](const Event &event) {
        if (event.source && event.source == event.target && event.size == 4) {
            int32_t n;
            std::memcpy(&n, event.data, 4);
            s.total += n;
        }
    });
    c->events(CollisionEnter, [&](const Event &) { s.total += 100; });
    c->events(CollisionStay, [&](const Event &) { s.total += 1; });
    s.total += s.cargo.front();
    std::rotate(s.cargo.begin(), s.cargo.begin() + 1, s.cargo.end());
    c->each<Counter>([&](Entity entity, Counter &out) {
        out.total = s.total;
        Event event{id("test.CheckpointEvent"), entity, entity, 4, {}};
        int32_t n = 7;
        std::memcpy(event.data, &n, 4);
        c->emit(event);
    });
}
bool save(const void *ptr, const ByteSink *sink, Error *) {
    auto &s = *static_cast<const Private *>(ptr);
    int32_t data[]{s.total, s.cargo[0], s.cargo[1], s.cargo[2]};
    return sink->write(sink->user, data, sizeof(data));
}
void *restore(const Context *, uint32_t version, const void *ptr, uint32_t size, Error *error) {
    if ((version != 1 && version != 2) || size != 16) {
        std::strcpy(error->message, "Unsupported private save version/size");
        return nullptr;
    }
    int32_t data[4];
    std::memcpy(data, ptr, 16);
    if (data[0] < 0 || data[1] < 0) {
        std::strcpy(error->message, "Invalid cargo");
        return nullptr;
    }
    auto s = new Private;
    s->total = data[0];
    s->cargo.assign(data + 1, data + 4);
    return s;
}
const Field fields[] = {GM_INT(Counter, total, "Total", 0, 0, 2147483647)};
const Component components[] = {component<Counter>("Saved", fields, 1)};
const System systems[] = {
    {"test.SavedSystem", "Private cargo", Phase::Late, nullptr, 0, create, destroy, update}};
const Module module{AbiVersion, sizeof(Module), "PersistenceFixture", components, 1, systems, 1};
const SystemState state{sizeof(SystemState), "test.SavedSystem", 2, save, restore};
const SystemState *states[]{&state};
const ModuleExtensions extra{sizeof(ModuleExtensions), 1, states, 1};
} // namespace
GM_GAME_EXPORT const Module *gm_game_module(uint32_t abi) {
    return abi == AbiVersion ? &module : nullptr;
}
GM_GAME_EXPORT const ModuleExtensions *gm_game_extensions(uint32_t version, const HostServices *host) {
    return version == 1 && host->service<StorageService>() ? &extra : nullptr;
}
