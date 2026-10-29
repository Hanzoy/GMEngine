#include "project.hpp"
#include "scene_catalog.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool near(float a, float b) {
    return std::abs(a - b) < .02f;
}
} // namespace
int main() {
    try {
        gm::useGameRegistry(std::make_shared<gm::GameRegistry>());
        auto source = gm::newScene();
        auto component = [](const char *type, gm::Json fields) {
            auto &d = gm::sceneComponent(type);
            for (const auto &field : d.fields)
                if (!fields.contains(field.key))
                    fields[field.key] = field.initial;
            return gm::Json{{"type", type}, {"version", d.version}, {"fields", fields}};
        };
        source["entities"].push_back(
            {{"id", "root"},
             {"name", "Root"},
             {"parent", nullptr},
             {"components",
              gm::Json::array({component("Transform2D", {{"x", 100}, {"y", 200}}),
                               component("Pose2D", {{"rotation", 90}, {"scaleX", 2}, {"scaleY", 2}}),
                               component("Collider2D", {{"width", 20}, {"height", 10}, {"layer", 2}}),
                               component("Camera2D", {{"zoom", 2}})})}});
        source["entities"].push_back(
            {{"id", "child"},
             {"name", "Child"},
             {"parent", "root"},
             {"components",
              gm::Json::array({component("Transform2D", {{"x", 10}, {"y", 20}}),
                               component("SpriteRenderer", gm::Json::object()),
                               component("SpriteRegion", gm::Json::object()),
                               component("SpriteAnimation",
                                         {{"columns", 4}, {"rows", 2}, {"frames", 8}, {"fps", 10}})})}});
        gm::SceneRuntime world(gm::compileScene(source, {}));
        auto p = world.position(1);
        check(near(p.x, 60) && near(p.y, 220), "Parent rotation/scale composition");
        auto camera = world.camera();
        check(near(camera.zoom, 2) && near(camera.transform.b, 2), "Camera transform");
        check(world.queryArea({79, 199, 22, 42}, 2).size() == 1, "Transformed trigger AABB");
        check(world.queryArea({79, 199, 22, 42}, 1).empty(), "Collision layer filter");
        auto hit = world.raycast(0, 220, 1, 0, 150, 2);
        check(hit.entity == world.entity(0) && near(hit.distance, 80), "Ray slab intersection");
        check(!world.raycast(0, 500, 1, 0, 150, 2).entity, "Ray miss");
        world.tick(.1f, {});
        check(near(world.sprites()[0].region.u0, .25f) && near(world.sprites()[0].region.u1, .5f),
              "Atlas animation");
        auto temp =
            std::filesystem::temp_directory_path() /
            ("gm-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        gm::saveScene(temp / "scenes/test.gmscene", world.snapshot());
        gm::saveCatalog(temp / "scenes.gmcatalog", {{"test-id", "test", "scenes/test.gmscene"}});
        gm::SceneLoader loader(temp / "scenes.gmcatalog");
        loader.request("test-id");
        std::unique_ptr<gm::CompiledScene> loaded;
        for (int i = 0; i < 1000 && !loaded; ++i) {
            loaded = loader.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(loaded && loaded->entities.size() == 2, "Asynchronous scene read and validation");
        {
            std::ofstream corrupt(temp / "scenes/test.gmscene", std::ios::binary);
            corrupt << "broken";
        }
        loader.request("test");
        bool rejected = false;
        for (int i = 0; i < 1000 && loader.pending(); ++i) {
            try {
                loader.poll();
            } catch (const std::exception &) {
                rejected = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(rejected && !loader.pending() && world.alive(0), "Failed async load retains the live World");
        gm::saveScene(temp / "scenes/test.gmscene", world.snapshot());
        loader.request("test");
        loaded.reset();
        for (int i = 0; i < 1000 && !loaded; ++i) {
            loaded = loader.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(loaded && loaded->entities.size() == 2, "Scene loader recovers after corrupt input");
        std::filesystem::remove_all(temp);
        std::cout << "2D hierarchy, camera, trigger queries, layers, raycast, atlas animation and async "
                     "scene catalog passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
