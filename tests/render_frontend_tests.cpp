#include "project.hpp"
#include "content_hash.hpp"
#include <random>
#include <iostream>
#include <iomanip>
#include <sstream>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string hash(const std::string &value) {
    auto words = gm::contentHash({reinterpret_cast<const uint8_t *>(value.data()), value.size()});
    std::ostringstream out;
    for (auto v : words)
        out << std::hex << std::setw(8) << std::setfill('0') << v;
    return out.str();
}
} // namespace
int main() {
    try {
        check(hash("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA256 empty vector");
        check(hash("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 abc vector");
        check(hash(std::string(1000000, 'a')) ==
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "SHA256 multi-block vector");
        gm::SpatialGrid grid;
        std::vector<gm::Rect> boxes;
        std::mt19937 random(173);
        std::uniform_real_distribution<float> position(-20000, 20000), extent(1, 1000);
        for (uint32_t row = 0; row < 10000; ++row) {
            boxes.push_back({position(random), position(random), extent(random), extent(random)});
            grid.set(row, boxes.back());
        }
        boxes[0] = {-100000, -100000, 200000, 200000};
        grid.set(0, boxes[0]);
        for (int n = 0; n < 200; ++n) {
            auto row = random() % boxes.size();
            boxes[row] = {position(random), position(random), extent(random), extent(random)};
            if (n % 5 == 0)
                boxes[row] = {};
            grid.set(uint32_t(row), boxes[row]);
            gm::Rect area{position(random), position(random), extent(random) * 2, extent(random) * 2};
            if (n == 0)
                area = {-30000, -30000, 60000, 60000};
            auto actual = grid.query(area);
            std::vector<uint32_t> expected;
            for (uint32_t i = 0; i < boxes.size(); ++i) {
                auto b = gm::intersect(area, boxes[i]);
                if (b.w > 0 && b.h > 0)
                    expected.push_back(i);
            }
            check(actual.rows == expected,
                  "Spatial index disagrees with brute force, order or deduplication");
            if (n == 0)
                check(actual.fallback, "Huge viewport must use bounded fallback");
            else
                check(actual.candidates < 1000, "Local query scanned the whole world");
        }
        gm::useGameRegistry(std::make_shared<gm::GameRegistry>());
        auto source = gm::newScene();
        auto comp = [](const char *name, gm::Json fields) {
            auto &desc = gm::sceneComponent(name);
            for (auto &field : desc.fields)
                if (!fields.contains(field.key))
                    fields[field.key] = field.initial;
            return gm::Json{{"type", name}, {"version", desc.version}, {"fields", fields}};
        };
        source["entities"].push_back(
            {{"id", "moving"},
             {"name", "Moving"},
             {"parent", nullptr},
             {"components", gm::Json::array({comp("Transform2D", {{"x", 0}, {"y", 0}}),
                                             comp("Velocity", {{"x", 60}, {"y", 0}}),
                                             comp("SpriteRenderer", {{"width", 20}, {"height", 20}}),
                                             comp("Camera2D", gm::Json::object())})}});
        gm::SceneRuntime world(gm::compileScene(source, {}));
        auto version = world.renderVersion();
        check(world.visibleSprites({0, 0, 100, 100}).rows.size() == 1, "Initial visibility");
        world.advance(.025, {});
        check(world.renderAlpha() > .49 && world.renderAlpha() < .51, "Fixed-step render alpha");
        auto sprite = world.sprites()[0];
        check(sprite.moving && sprite.previous.x == 0 && std::abs(sprite.transform.x - 1) < .001,
              "Previous/current render transforms");
        check(std::abs(world.renderCamera().transform.x - .5) < .001,
              "Camera translation interpolates with sprites");
        check(world.visibleSprites({-.1f, 0, .2f, 20}).rows.size() == 1,
              "Visibility includes previous position during interpolation");
        check(world.renderVersion() != version, "Moving sprite invalidates render version");
        world.resetClock();
        check(world.renderAlpha() == 1, "Pause/reset shows current simulation state");
        world.tick(1.f / 60, {});
        check(world.renderAlpha() == 1, "Explicit single-step is not interpolated");
        auto saved = world.snapshot();
        world.restore(saved);
        check(world.visibleSprites({0, 0, 100, 100}).rows.size() == 1, "Restore rebuilds index");
        world.destroy(world.entity(0));
        check(world.visibleSprites({0, 0, 100, 100}).rows.empty(), "Destroyed sprite removed from index");
        world.restore(saved);
        check(world.visibleSprites({0, 0, 100, 100}).rows.size() == 1,
              "Same-sized replacement rebuilds index");
        gm::sdk::Pose2D pose;
        pose.rotation = 90;
        world.set(world.entity(0), gm::sdk::id(gm::sdk::Pose2D::gm_name),
                  gm::gameRegistry()->encode(gm::sdk::id(gm::sdk::Pose2D::gm_name), &pose));
        check(world.visibleSprites({-15, 5, 1, 1}).rows.size() == 1 &&
                  world.visibleSprites({10, 5, 1, 1}).rows.empty(),
              "Rotated sprite bounds were not indexed conservatively");
        check(!gm::runtimeCommand(world, {{"op", "runtime.render.stats"}})["result"]["available"].get<bool>(),
              "Headless GPU statistics must be unavailable");
        std::cout << "PASS: content hashes, 200 spatial/brute-force comparisons, swept bounds, "
                     "interpolation, restore and destruction\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
