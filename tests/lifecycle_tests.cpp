#include "project.hpp"
#include <iostream>
#include <cmath>
#include <bit>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool ok = false;
    try {
        f();
    } catch (const std::exception &) {
        ok = true;
    }
    check(ok, "Expected rejection");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Module path required");
        auto registry = std::make_shared<gm::GameRegistry>();
        registry->load(argv[1]);
        gm::useGameRegistry(registry);
        auto source = gm::newScene();
        source["entities"].push_back(
            {{"id", "root"},
             {"name", "Root"},
             {"parent", nullptr},
             {"components",
              gm::Json::array(
                  {{{"type", "Transform2D"}, {"version", 1}, {"fields", {{"x", 100}, {"y", 200}}}},
                   {{"type", "test.Probe"}, {"version", 1}, {"fields", {{"received", 0}}}}})}});
        auto compiled = gm::compileScene(source, {});
        gm::SceneRuntime run(compiled);
        run.tick(1.f / 60, {});
        check(run.stats().entities == 2, "Create/set inside query failed");
        check(run.component(0, gm::sdk::id("test.Probe"))[0] == 0,
              "Events must not be visible in producing tick");
        check(std::abs(run.position(1).y - 230) < .01, "Dynamic child hierarchy");
        run.tick(1.f / 60, {});
        check(run.component(0, gm::sdk::id("test.Probe"))[0] == 1, "Event visible next tick");
        auto stale = run.entity(1);
        run.tick(1.f / 60, {});
        check(run.stats().entities == 1 && !run.alive(1), "Destroy inside query");
        rejects([&] { run.destroy(stale); });
        auto fresh = run.create(run.entity(0));
        auto velocity = gm::componentWords("Velocity", {{"x", 0}, {"y", 1}});
        run.set(fresh, gm::sdk::id("Velocity"), velocity);
        check(run.entities().back().components.size() == 2, "Runtime dependency composition");
        rejects([&] { run.remove(fresh, gm::sdk::id("Transform2D")); });
        run.remove(fresh, gm::sdk::id("Velocity"));
        run.remove(fresh, gm::sdk::id("Transform2D"));
        auto snapshot = run.snapshot();
        run.destroy(run.entity(0));
        check(run.stats().entities == 0, "Destroy subtree");
        run.restore(snapshot);
        check(run.stats().entities == 2, "Restore compacted state");
        auto before = run.snapshot();
        auto invalid = before;
        invalid.schema ^= 1;
        rejects([&] { run.restore(invalid); });
        check(run.stats().entities == 2, "Invalid restore must preserve current scene");
        gm::SceneRuntime fast(compiled), slow(compiled);
        for (int i = 0; i < 144; ++i)
            fast.advance(1. / 144, {});
        for (int i = 0; i < 30; ++i)
            slow.advance(1. / 30, {});
        check(fast.ticks() == 60 && slow.ticks() == 60, "Fixed simulation independent of render cadence");
        auto ticks = slow.ticks();
        check(slow.advance(10, {}) == 8 && slow.ticks() == ticks + 8 && slow.stats().droppedSeconds > 9,
              "Catch-up cap");
        check(slow.stats().systems[0].calls == 68, "System profiler");
        rejects([&] { slow.advance(-1, {}); });
        run.restore(compiled);
        auto clone = run.instantiate(0);
        check(run.stats().entities == 2, "Authored prefab instantiate");
        run.destroy(clone);
        check(run.stats().entities == 1, "Prefab teardown");
        for (int i = 0; i < 256; ++i) {
            auto e = run.create();
            run.destroy(e);
        }
        check(run.entities().size() <= 2, "Runtime slots must be reused");
        auto parent = run.create();
        for (int i = 0; i < 256; ++i)
            run.create(parent);
        run.destroy(parent);
        check(run.stats().entities == 1, "Wide hierarchy destruction");
        gm::SceneRuntime collision(compiled);
        collision.tick(1.f / 60, {});
        auto collider = gm::componentWords("Collider2D", {{"width", 64},
                                                          {"height", 64},
                                                          {"offsetX", 0},
                                                          {"offsetY", 0},
                                                          {"layer", 1},
                                                          {"mask", 1},
                                                          {"enabled", true}});
        collision.set(collision.entity(0), gm::sdk::id("Collider2D"), collider);
        collision.set(collision.entity(1), gm::sdk::id("Collider2D"), collider);
        for (int i = 0; i < 3; ++i)
            collision.tick(1.f / 60, {});
        check(collision.component(0, gm::sdk::id("test.Probe"))[0] == 31,
              "Trigger enter/exit events and destroyed source lifetime");
        run.restore(compiled);
        run.set(run.entity(0), gm::sdk::id("test.Probe"), std::array<uint32_t, 1>{99});
        bool violation = false;
        try {
            run.tick(1.f / 60, {});
        } catch (const std::exception &e) {
            auto text = std::string(e.what());
            violation = text.find("test.Runtime at tick") != std::string::npos &&
                        text.find("undeclared") != std::string::npos;
        }
        check(violation, "System access contract and error attribution");
        std::cout << "Deferred structure, generation handles, dependencies, hierarchy, events, snapshot "
                     "isolation, fixed step and profiling passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
