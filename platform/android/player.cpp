#include "scene_view.hpp"
#include "portable_font.hpp"
#include "audio.hpp"
#include <gm/input.hpp>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
extern "C" const gm::sdk::Module *gm_game_module(uint32_t);
extern "C" const gm::sdk::ModuleExtensions *gm_game_extensions(uint32_t, const gm::sdk::HostServices *)
    __attribute__((weak));
namespace {
using Clock = std::chrono::steady_clock;
struct Player {
    android_app *app;
    std::unique_ptr<gm::SceneRuntime> scene;
    std::unique_ptr<gm::Renderer> renderer;
    std::unique_ptr<gm::SceneView> view;
    gm::sdk::TouchInput touches;
    bool resumed = false, focused = false;
    Clock::time_point last = Clock::now();
    std::filesystem::path root;
    explicit Player(android_app *a) : app(a) {
        setenv("GM_ENGINE_USER_DATA", app->activity->internalDataPath, 1);
        root = std::filesystem::path(app->activity->internalDataPath) / "content";
        std::filesystem::create_directories(root);
        auto registry = std::make_shared<gm::GameRegistry>();
        registry->loadStatic(gm_game_module, gm_game_extensions);
        gm::useGameRegistry(registry);
        auto versionBytes = asset("content.version");
        std::string version(versionBytes.begin(), versionBytes.end()), installed;
        std::ifstream(root / ".version") >> installed;
        const bool extract = installed != version || !std::filesystem::is_regular_file(root / "game.gmscene");
        uint64_t total = extract ? copy("game.gmscene") : 0;
        if (auto manifest = asset("content.files", false); extract && !manifest.empty()) {
            std::istringstream lines(std::string(manifest.begin(), manifest.end()));
            std::string file;
            while (std::getline(lines, file)) {
                if (!file.empty() && file.back() == '\r')
                    file.pop_back();
                if (file.empty())
                    continue;
                total += copy(file.c_str());
                if (total > 256 * 1024 * 1024)
                    throw std::runtime_error("Android content exceeds 256 MiB");
            }
        }
        if (extract) {
            std::ofstream marker(root / ".version", std::ios::trunc);
            marker << version;
            marker.flush();
            if (!marker)
                throw std::runtime_error("Cannot record Android content version");
        }
        if (auto font = asset("font.ttf", false); !font.empty())
            gm::setPortableFont(std::move(font));
        scene = std::make_unique<gm::SceneRuntime>(gm::loadScene(root / "game.gmscene"), root);
        try {
            scene->loadSlot("android-suspend");
        } catch (const std::exception &e) {
            __android_log_print(ANDROID_LOG_INFO, "GMEngine", "Starting entry scene: %s", e.what());
        }
    }
    ~Player() {
        view.reset();
        renderer.reset();
        scene.reset();
    }
    std::vector<uint8_t> asset(const char *name, bool required = true) {
        AAsset *file = AAssetManager_open(app->activity->assetManager, name, AASSET_MODE_STREAMING);
        if (!file) {
            if (!required)
                return {};
            throw std::runtime_error(std::string("Missing Android asset: ") + name);
        }
        auto size = AAsset_getLength64(file);
        if (size < 0 || size > 192 * 1024 * 1024) {
            AAsset_close(file);
            throw std::runtime_error("Invalid Android asset size");
        }
        std::vector<uint8_t> bytes(size_t(size), 0);
        size_t offset = 0;
        while (offset < bytes.size()) {
            int n = AAsset_read(file, bytes.data() + offset,
                                std::min<size_t>(1024 * 1024, bytes.size() - offset));
            if (n <= 0) {
                AAsset_close(file);
                throw std::runtime_error("Cannot read Android asset");
            }
            offset += size_t(n);
        }
        AAsset_close(file);
        return bytes;
    }
    size_t copy(const char *name) {
        std::filesystem::path relative(name);
        if (relative.is_absolute() || relative.empty() || *relative.lexically_normal().begin() == "..")
            throw std::runtime_error("Unsafe Android content path");
        auto bytes = asset(name);
        auto destination = root / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot extract Android content");
        return bytes.size();
    }
    gm::Rect viewport() const {
        auto r = app->contentRect;
        float w = float(ANativeWindow_getWidth(app->window)), h = float(ANativeWindow_getHeight(app->window));
        if (r.right <= r.left || r.bottom <= r.top)
            return {0, 0, w, h};
        return gm::intersect({float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top)},
                             {0, 0, w, h});
    }
    void command(int32_t cmd) {
        switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                renderer = std::make_unique<gm::Renderer>(app->window, ANativeWindow_getWidth(app->window),
                                                          ANativeWindow_getHeight(app->window));
                view = std::make_unique<gm::SceneView>(*renderer);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            view.reset();
            renderer.reset();
            touches.clear();
            break;
        case APP_CMD_RESUME:
            resumed = true;
            last = Clock::now();
            scene->resetClock();
            gm::pauseAudioForCurrentThread(false);
            break;
        case APP_CMD_PAUSE:
            resumed = false;
            touches.clear();
            gm::pauseAudioForCurrentThread(true);
            try {
                scene->saveSlot("android-suspend");
            } catch (const std::exception &e) {
                __android_log_print(ANDROID_LOG_WARN, "GMEngine", "Suspend save: %s", e.what());
            }
            break;
        case APP_CMD_GAINED_FOCUS:
            focused = true;
            break;
        case APP_CMD_LOST_FOCUS:
            focused = false;
            touches.clear();
            break;
        default:
            break;
        }
    }
    int32_t input(AInputEvent *event) {
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
            return 0;
        int action = AMotionEvent_getAction(event), kind = action & AMOTION_EVENT_ACTION_MASK;
        if (kind == AMOTION_EVENT_ACTION_CANCEL) {
            touches.clear();
            return 1;
        }
        auto index = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        size_t count = std::min<size_t>(AMotionEvent_getPointerCount(event), 10);
        for (size_t i = 0; i < count; ++i) {
            bool down =
                !((kind == AMOTION_EVENT_ACTION_UP || kind == AMOTION_EVENT_ACTION_POINTER_UP) && i == index);
            touches.set(AMotionEvent_getPointerId(event, i), AMotionEvent_getX(event, i),
                        AMotionEvent_getY(event, i), down);
        }
        return 1;
    }
    bool active() const {
        return renderer && resumed && app->window;
    }
    void frame() {
        if (!active())
            return;
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        auto area = viewport();
        if (area.w <= 0 || area.h <= 0)
            return;
        gm::SceneInput input;
        input.pointer.focused = focused;
        touches.sample(input.pointer);
        float width = scene->asset().width, height = scene->asset().height,
              scale = std::min(area.w / width, area.h / height);
        for (uint32_t i = 0; i < input.pointer.touchCount; ++i) {
            auto &t = input.pointer.touches[i];
            t.x = (t.x - area.x - (area.w - width * scale) * .5f) / scale;
            t.y = (t.y - area.y - (area.h - height * scale) * .5f) / scale;
        }
        scene->advance(dt, input);
        renderer->begin(uint32_t(ANativeWindow_getWidth(app->window)),
                        uint32_t(ANativeWindow_getHeight(app->window)));
        view->render(*scene, area);
        renderer->end();
    }
};
} // namespace
void android_main(android_app *app) {
    app_dummy();
    try {
        Player player(app);
        app->userData = &player;
        app->onAppCmd = [](android_app *a, int32_t cmd) { static_cast<Player *>(a->userData)->command(cmd); };
        app->onInputEvent = [](android_app *a, AInputEvent *e) {
            return static_cast<Player *>(a->userData)->input(e);
        };
        while (!app->destroyRequested) {
            android_poll_source *source = nullptr;
            int events = 0;
            int result = ALooper_pollOnce(player.active() ? 0 : -1, nullptr, &events,
                                          reinterpret_cast<void **>(&source));
            if (result >= 0 && source)
                source->process(app, source);
            if (!app->destroyRequested)
                player.frame();
        }
        app->userData = nullptr;
    } catch (const std::exception &e) {
        __android_log_print(ANDROID_LOG_ERROR, "GMEngine", "Player failed: %s", e.what());
        ANativeActivity_finish(app->activity);
    }
}
