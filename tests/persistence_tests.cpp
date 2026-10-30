#include "project.hpp"
#include "checkpoint.hpp"
#include "process.hpp"
#include <gm/ui_input.hpp>
#include <iostream>
#include <chrono>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try {
        fn();
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--lease-child") {
            try {
                gm::StorageSession busy("Contract", "profile");
                return 2;
            } catch (...) {
                return 0;
            }
        }
        check(argc == 2, "Expected module");
        auto registry = std::make_shared<gm::GameRegistry>();
        registry->load(argv[1]);
        gm::useGameRegistry(registry);
        auto source = gm::newScene();
        source["entities"].push_back(
            {{"id", "saved"},
             {"name", "Saved"},
             {"parent", nullptr},
             {"components",
              gm::Json::array({{{"type", "test.Saved"}, {"version", 1}, {"fields", {{"total", 0}}}}})}});
        for (auto name : {"left", "right"}) {
            gm::Json transform = {{"type", "Transform2D"}, {"version", 1}, {"fields", {{"x", 0}, {"y", 0}}}};
            gm::Json collider = {{"type", "Collider2D"},
                                 {"version", 1},
                                 {"fields",
                                  {{"width", 20},
                                   {"height", 20},
                                   {"offsetX", 0},
                                   {"offsetY", 0},
                                   {"layer", 1},
                                   {"mask", 65535},
                                   {"enabled", true}}}};
            source["entities"].push_back({{"id", name},
                                          {"name", name},
                                          {"parent", nullptr},
                                          {"components", gm::Json::array({transform, collider})}});
        }
        gm::SceneRuntime run(gm::compileScene(source, {}));
        auto transient = run.create();
        run.destroy(transient);
        run.create();
        for (int i = 0; i < 7; ++i)
            run.tick(1.f / 60, {});
        run.advance(1. / 240, {});
        auto bytes = run.checkpoint();
        {
            auto saved = gm::decodeCheckpoint(bytes);
            check(saved.events.size() == 3 && saved.contacts.size() == 1,
                  "Pending events and active contact captured");
        }
        run.create();
        for (int i = 0; i < 19; ++i)
            run.tick(1.f / 60, {});
        auto continued = run.checkpoint();
        run.restoreCheckpoint(bytes);
        check(run.ticks() == 7, "Tick counter restored");
        run.create();
        for (int i = 0; i < 19; ++i)
            run.tick(1.f / 60, {});
        check(run.checkpoint() == continued, "Private in-flight cargo resumes deterministically");
        auto invalid = bytes;
        invalid.back() ^= 1;
        rejects([&] { run.restoreCheckpoint(invalid); });
        check(run.checkpoint() == continued, "Corrupt file must preserve live World");
        auto save = gm::decodeCheckpoint(bytes);
        save.systems[0].version = 99;
        rejects([&] { run.restoreCheckpoint(gm::encodeCheckpoint(save)); });
        check(run.checkpoint() == continued, "Callback migration rejection must preserve live World");
        save.systems[0].version = 1;
        run.restoreCheckpoint(gm::encodeCheckpoint(save));
        check(run.ticks() == 7, "Project callback accepts older private state version");
        save.systems.clear();
        rejects([&] { run.restoreCheckpoint(gm::encodeCheckpoint(save)); });
        check(run.ticks() == 7, "Missing private state rejected transactionally");
        auto root =
            std::filesystem::temp_directory_path() /
            ("gm-storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
        SetEnvironmentVariableW(L"GM_ENGINE_USER_DATA", root.c_str());
#else
        setenv("GM_ENGINE_USER_DATA", root.c_str(), 1);
#endif
        {
            gm::StorageSession storage("Contract", "profile");
            rejects([&] { gm::StorageSession concurrent("Contract", "profile"); });
#ifdef _WIN32
            check(gm::runProcess({std::filesystem::absolute(argv[0]).wstring(), L"--lease-child"}, root,
                                 root / "child.log") == 0,
                  "Writer lease excludes another process");
#endif
            rejects([&] { storage.write("../escape", bytes); });
            storage.write("slot", bytes);
            storage.write("slot", continued);
            check(storage.read("slot") == continued && storage.read("slot.bak") == bytes,
                  "Atomic replacement retains previous complete backup");
            storage.write("blocked", bytes);
            std::filesystem::create_directory(storage.path("blocked.bak"));
            rejects([&] { storage.write("blocked", continued); });
            check(storage.read("blocked") == bytes, "Backup failure leaves primary save untouched");
            auto reader = gm::hostServices().service<gm::sdk::StorageService>();
            check(reader != nullptr && gm::hostServices().query(gm::sdk::StorageService::Id, 99) == nullptr,
                  "Service version negotiation");
        }
        {
            gm::StorageSession reopened("Contract", "profile");
            check(reopened.read("slot") == continued, "Writer lease released");
        }
        {
            auto path = run.saveSlot("slot");
            gm::SceneRuntime other(gm::compileScene(source, {}));
            rejects([&] { other.saveSlot("slot"); });
            run.tick(1.f / 60, {});
            run.loadSlot("slot");
            check(run.ticks() == 7, "Shared runtime save/load uses private checkpoint");
        }
        using namespace gm::sdk;
        UiInputRouter router;
        auto background = id("background"), modal = id("modal"), button = id("modal.button");
        router.define({background, 0, {0, 0, 800, 600}});
        router.define({modal, 0, {100, 100, 500, 400}});
        router.define({button, modal, {120, 120, 60, 40}});
        Input input;
        input.focused = 1;
        input.mouseX = 130;
        input.mouseY = 130;
        input.pressed = input.buttons = 1;
        input.keyPress['R' / 32] = 1u << ('R' % 32);
        router.modal(modal);
        router.begin(input);
        check(router.sample(button).pressed == 1 && router.sample(background).pressed == 0 &&
                  !router.sample(background).hit('R'),
              "Modal excludes background pointer and shortcut input");
        router.capture(button);
        input.mouseX = 790;
        input.mouseY = 590;
        input.pressed = 0;
        input.released = 1;
        input.buttons = 0;
        router.begin(input);
        check(router.sample(button).released == 1, "Pointer capture retains release outside bounds");
        router.begin({});
        check(router.pointerOwner() == 0 && router.keyboardOwner() == 0, "Focus loss clears capture");
        router.begin(input);
        check(router.sample(background).released == 0, "Outside-modal release cannot leak to background");
        router.modal(0);
        router.begin(input);
        check(router.sample(background).released == 1, "Clearing modal restores background input");
        router.consumePointer(background);
        check(router.sample(background).released == 0, "Consumed event cannot be delivered twice");
        check(!router.sample(0).focused && !router.sample(id("unknown")).focused,
              "Unknown input contexts cannot acquire raw input");
        router.enable(modal, false);
        router.begin(input);
        check(router.keyboardOwner() == background, "Disabled parent suppresses child shortcuts");
        router.enable(modal, true);
        router.focus(button);
        router.begin(input);
        check(router.keyboardOwner() == button, "Explicit keyboard focus is retained");
        std::cout << "PASS private-state checkpoints, migration rollback, storage lease/backup, "
                     "modal/capture input\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
