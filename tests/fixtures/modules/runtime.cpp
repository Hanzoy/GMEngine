#include <gm/game.hpp>
using namespace gm::sdk;
struct Probe {
    static constexpr const char *gm_name = "test.Probe";
    int32_t received = 0;
};
namespace {
const Field fields[] = {GM_INT(Probe, received, "Received", 0, 0, 100)};
const Component components[] = {component<Probe>("Runtime contract probe", fields, 1)};
void update(void *, const Context *c) {
    c->each<Probe>([&](Entity entity, Probe &probe) {
        if (probe.received == 99)
            c->get<Camera2D>(entity);
        c->events(id("test.Event"), [&](const Event &event) {
            if (event.target == entity)
                ++probe.received;
        });
        c->events(CollisionEnter, [&](const Event &event) {
            if (event.target == entity)
                probe.received += 10;
        });
        c->events(CollisionExit, [&](const Event &event) {
            if (event.target == entity)
                probe.received += 20;
        });
        if (c->frame->tick == 1) {
            auto child = c->create(entity);
            c->set(child, Transform2D{20, 30});
            c->set(child, Velocity{5, 0});
            Event event{id("test.Event"), child, entity, 0, {}};
            c->emit(event);
        }
    });
    if (c->frame->tick == 3)
        c->each<const Velocity>([&](Entity entity, const Velocity &) { c->destroy(entity); });
}
const Term access[] = {{id(Probe::gm_name), 1}, {id(Transform2D::gm_name), 1}, {id(Velocity::gm_name), 1}};
const System systems[] = {
    {"test.Runtime", "Runtime", Phase::Input, nullptr, 0, nullptr, nullptr, update, access, 3}};
const Module module{AbiVersion, sizeof(Module), "RuntimeContract", components, 1, systems, 1};
} // namespace
GM_GAME_EXPORT const Module *gm_game_module(uint32_t abi) {
    return abi == AbiVersion ? &module : nullptr;
}
