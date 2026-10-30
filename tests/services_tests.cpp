#include "audio.hpp"
#include "game_jobs.hpp"
#include "ui_compiler.hpp"
#include "runtime_layout.hpp"
#include "path_text.hpp"
#include "resources.hpp"
#include "project.hpp"
#include <gm/input.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
void check(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
void word(std::ostream &s, uint32_t v, int count) {
    while (count--) {
        s.put(char(v));
        v >>= 8;
    }
}
gm::sdk::Error error{};
void audio() {
    auto file = std::filesystem::temp_directory_path() / std::filesystem::path(u8"gm-音频-🧪-contract.wav");
    {
        std::ofstream out(file, std::ios::binary);
        out.write("RIFF", 4);
        word(out, 36 + 9600, 4);
        out.write("WAVEfmt ", 8);
        word(out, 16, 4);
        word(out, 1, 2);
        word(out, 1, 2);
        word(out, 48000, 4);
        word(out, 96000, 4);
        word(out, 2, 2);
        word(out, 16, 2);
        out.write("data", 4);
        word(out, 9600, 4);
        for (int i = 0; i < 4800; ++i)
            word(out, uint16_t(int16_t(12000 * std::sin(i * 6.283185307 * 440 / 48000))), 2);
    }
    const auto &api = gm::audioService();
    gm::sdk::AudioConfig config;
    config.headless = 1;
    void *session = api.open(&config, &error);
    check(session, error.message);
    try {
        auto clip = api.load(session, gm::pathText(file).c_str(), false, &error);
        check(clip, error.message);
        auto voice = api.play(session, clip, gm::sdk::AudioBus::Effects, true, 1, &error);
        check(voice, error.message);
        check(!api.unload(session, clip, &error), "Live clip unload must fail");
        std::vector<float> pcm(9600);
        check(api.render(session, pcm.data(), 4800, &error), error.message);
        double energy = 0;
        for (auto v : pcm)
            energy += v * v;
        check(energy > 10, "PCM output must contain audio");
        check(api.volume(session, gm::sdk::AudioBus::Effects, 0, &error), error.message);
        check(api.render(session, pcm.data(), 4800, &error), error.message);
        check(api.render(session, pcm.data(), 4800, &error), error.message);
        for (auto v : pcm)
            check(std::abs(v) < .00001f, "Bus mute must produce silence");
        check(api.pause(session, voice, true, &error) && !api.playing(session, voice), "Pause");
        check(api.collect(session).voices == 1, "Paused voices retained");
        check(api.pause(session, voice, false, &error) && api.playing(session, voice), "Resume");
        check(api.suspend(session, true, &error) && api.render(session, pcm.data(), 4800, &error), "Suspend");
        for (auto v : pcm)
            check(v == 0, "Suspend silence");
        check(api.suspend(session, false, &error), "Resume engine");
        check(api.stop(session, voice, &error) && api.unload(session, clip, &error), "Voice/clip teardown");
        clip = api.load(session, gm::pathText(file).c_str(), true, &error);
        check(clip, error.message);
        voice = api.play(session, clip, gm::sdk::AudioBus::Music, false, 1, &error);
        check(voice, error.message);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (api.collect(session).voices) {
            check(std::chrono::steady_clock::now() < deadline, "Streaming completion deadline");
            check(api.render(session, pcm.data(), 4800, &error), error.message);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(api.collect(session).voices == 0, "Completed streaming music must be reclaimed");
        check(api.unload(session, clip, &error), error.message);
    } catch (...) {
        api.close(session);
        std::filesystem::remove(file);
        throw;
    }
    api.close(session);
    std::filesystem::remove(file);
}
void jobs() {
    const auto &api = gm::jobService();
    void *group = api.open(&error);
    check(group, error.message);
    try {
        int input = 21;
        auto id = api.submit(
            group, "double",
            [](const void *p, uint32_t n, const gm::sdk::JobControl *, const gm::sdk::ByteSink *sink,
               gm::sdk::Error *) {
                if (n != sizeof(int))
                    return false;
                int value;
                std::memcpy(&value, p, n);
                value *= 2;
                return sink->write(sink->user, &value, sizeof(value));
            },
            &input, sizeof(input), &error);
        check(id, error.message);
        input = 99;
        gm::sdk::JobResult result;
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!api.poll(group, &result)) {
            check(std::chrono::steady_clock::now() < until, "Job deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        int value = 0;
        check(result.status == gm::sdk::JobStatus::Succeeded && result.size == sizeof(value), "Job result");
        std::memcpy(&value, result.data, sizeof(value));
        check(value == 42, "Owned input/output copies");
        id = api.submit(
            group, "cancel",
            [](const void *, uint32_t, const gm::sdk::JobControl *c, const gm::sdk::ByteSink *,
               gm::sdk::Error *) {
                while (!c->cancelled(c->state))
                    std::this_thread::yield();
                return true;
            },
            nullptr, 0, &error);
        check(id && api.cancel(group, id), "Cancel game job");
        until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!api.poll(group, &result)) {
            check(std::chrono::steady_clock::now() < until, "Cancel deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(result.status == gm::sdk::JobStatus::Cancelled, "Cancellation status");
        check(api.submit(
                  group, "close joins",
                  [](const void *, uint32_t, const gm::sdk::JobControl *c, const gm::sdk::ByteSink *,
                     gm::sdk::Error *) {
                      while (!c->cancelled(c->state))
                          std::this_thread::yield();
                      return true;
                  },
                  nullptr, 0, &error) != 0,
              "Submit before close");
    } catch (...) {
        api.close(group);
        throw;
    }
    api.close(group);
}
void ui() {
    gm::Json source = {{"format", "gm.ui"},
                       {"version", 1},
                       {"root",
                        {{"id", "root"},
                         {"type", "panel"},
                         {"x", 0},
                         {"y", 0},
                         {"w", 640},
                         {"h", 480},
                         {"layout", "column"},
                         {"padding", 12},
                         {"gap", 8},
                         {"children", gm::Json::array({{{"id", "status"},
                                                        {"type", "label"},
                                                        {"x", 0},
                                                        {"y", 0},
                                                        {"w", 200},
                                                        {"h", 24},
                                                        {"bindText", "statusText"}},
                                                       {{"id", "rows"},
                                                        {"type", "list"},
                                                        {"x", 0},
                                                        {"y", 0},
                                                        {"w", 200},
                                                        {"h", 100},
                                                        {"fillX", true},
                                                        {"event", "ui.choose"},
                                                        {"rowHeight", 25}}})}}}};
    auto compiled = gm::compileUi(source);
    auto bytes = gm::encodeUi(compiled);
    gm::RuntimeDocument doc(gm::decodeUi(bytes));
    gm::RuntimeLayout layout;
    layout.update(doc, 640, 480);
    check(layout.boxes()[1].x == 12 && layout.boxes()[2].y == 44 && layout.boxes()[2].w == 616,
          "Column layout, padding, gap and fill");
    auto builds = layout.rebuilds();
    doc.bindText("statusText", "中文联机状态");
    layout.update(doc, 640, 480);
    check(doc.nodes()[1].text == "中文联机状态" && layout.rebuilds() == builds,
          "String bindings must not rebuild layout");
    const char *rows[] = {"one", "two", "three", "four", "five", "six"};
    doc.setList(2, rows, 6);
    doc.scroll(2, 50);
    check(layout.listRow(doc, 2, 45) == 2, "Scrolled row selection");
    doc.scroll(2, 99999);
    check(doc.scrollOffset(2) == 50, "Scroll clamp");
    doc.setVisible(1, false);
    layout.update(doc, 640, 480);
    check(layout.boxes()[2].y == 12, "Hidden children leave flow");
    check(layout.hit(20, 20) == 2, "List event hit region");
    doc.setList(2, nullptr, 0);
    check(doc.scrollOffset(2) == 0 && layout.listRow(doc, 2, 20) == -1, "Empty list clamp/hit");
    source["root"]["layout"] = "unknown";
    bool rejected = false;
    try {
        gm::compileUi(source);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Invalid layout rejected");
}
void input() {
    using namespace gm::sdk;
    ActionMap actions;
    actions.bind({id("move"), id("game"), InputSource::Key, 'A', 0, -1});
    actions.bind({id("move"), id("game"), InputSource::Key, 'D'});
    actions.bind({id("move"), id("game"), InputSource::TouchRegion, 0, 0, 1, .15f, 100, 0, 100, 100});
    actions.bind({id("pad"), id("game"), InputSource::PadAxis, 0});
    Input state;
    state.focused = 1;
    state.keys['D' / 32] |= 1u << ('D' % 32);
    actions.update(state);
    check(actions.get(id("move")).pressed && actions.get(id("move")).value == 1, "Action key binding");
    actions.update(state);
    check(!actions.get(id("move")).pressed, "Action edge once");
    actions.modal(id("menu"));
    actions.update(state);
    check(actions.get(id("move")).released, "Modal action context");
    actions.modal();
    state = {};
    state.focused = 1;
    TouchInput touches;
    touches.set(42, 120, 10, true);
    touches.set(7, 20, 10, true);
    touches.sample(state);
    check(state.touchCount == 2 && state.touches[0].id == 42 && state.touches[1].id == 7,
          "Stable independent touch IDs");
    actions.update(state);
    check(actions.get(id("move")).held, "Touch action");
    touches.set(42, 120, 10, false);
    touches.sample(state);
    check(state.touchCount == 2 && state.touches[0].released, "Touch release retained");
    actions.update(state);
    check(actions.get(id("move")).released, "Touch action release");
    touches.sample(state);
    check(state.touchCount == 1 && state.touches[0].id == 7, "Remaining finger keeps ID");
    state.gamepads[0].connected = 1;
    state.gamepads[0].axes[0] = .1f;
    actions.update(state);
    check(actions.get(id("pad")).value == 0, "Gamepad deadzone");
    state.gamepads[0].axes[0] = 1;
    actions.update(state);
    check(actions.get(id("pad")).value == 1, "Gamepad axis");
    state.focused = 0;
    actions.update(state);
    check(actions.get(id("pad")).released, "Focus loss clears actions");
}
void resourcesAndPrefabs() {
    using namespace gm;
    auto registry = std::make_shared<GameRegistry>();
    useGameRegistry(registry);
    auto source = newScene();
    source["entities"].push_back(
        {{"id", "root"},
         {"name", "Root"},
         {"parent", nullptr},
         {"components",
          Json::array({{{"type", "Transform2D"}, {"version", 1}, {"fields", {{"x", 12}, {"y", 34}}}}})}});
    auto prefab = compileScene(source, {});
    auto bytes = encodeScene(prefab);
    auto folder = std::filesystem::temp_directory_path() / "gm-resource-contract";
    std::filesystem::create_directories(folder);
    auto file = folder / "object.gmprefab";
    saveScene(file, prefab);
    const auto &api = resourceService();
    void *a = api.open(pathText(folder).c_str(), &error), *b = api.open(pathText(folder).c_str(), &error);
    check(a && b, error.message);
    try {
        check(!api.request(a, "../missing.gmprefab", &error), "Resource path escape rejected");
        auto first = api.request(a, "object.gmprefab", &error),
             second = api.request(b, "object.gmprefab", &error);
        check(first && second, error.message);
        sdk::ResourceData x, y;
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            check(api.poll(a, first, &x, &error) && api.poll(b, second, &y, &error), error.message);
            check(std::chrono::steady_clock::now() < until, "Resource deadline");
            std::this_thread::yield();
        } while (x.status == sdk::ResourceStatus::Pending || y.status == sdk::ResourceStatus::Pending);
        check(x.status == sdk::ResourceStatus::Ready && x.data == y.data && x.size == bytes.size(),
              "Shared immutable resource buffer");
        SceneRuntime world(compileScene(newScene(), {}));
        auto id = world.loadPrefab({static_cast<const uint8_t *>(x.data), x.size});
        auto entity = world.instantiatePrefab(id);
        check(world.stats().entities == 1, "External prefab instantiated");
        check(world.position(world.find(world.entityId(entity))).x == 12, "Prefab component values");
        world.unloadPrefab(id);
        check(world.stats().entities == 1, "Prefab unload preserves live entities");
        auto before = world.stats().entities;
        bool rejected = false;
        try {
            world.instantiatePrefab(id);
        } catch (...) {
            rejected = true;
        }
        check(rejected && world.stats().entities == before, "Stale prefab handle rejected without mutation");
        check(api.release(a, first, &error), error.message);
        check(api.poll(b, second, &y, &error) && y.status == sdk::ResourceStatus::Ready,
              "Other resource lease remains valid");
        check(api.release(b, second, &error), error.message);
        check(!api.poll(b, second, &y, &error), "Released resource handle rejected");
        for (int i = 0; i < 140; ++i)
            world.tick(1.f / 60, {});
        auto stats = world.stats();
        check(stats.sampledFrames == 120 && stats.p95Ms >= stats.p50Ms, "Bounded profiler history");
    } catch (...) {
        api.close(a);
        api.close(b);
        std::filesystem::remove(file);
        std::filesystem::remove(folder);
        throw;
    }
    api.close(a);
    api.close(b);
    std::filesystem::remove(file);
    std::filesystem::remove(folder);
}
} // namespace
int main() {
    try {
        jobs();
        ui();
        audio();
        input();
        resourcesAndPrefabs();
        std::cout << "Game jobs, UI flow/bindings/lists, audio PCM/mixing/streaming, action maps/multitouch, "
                     "shared async resources, external prefabs and profiling passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
