// Deterministic native scene rendering for game QA. Renders its own hidden surface;
// it never captures the desktop, injects OS input, or interacts with other apps.
#include "project.hpp"
#include "scene_view.hpp"
#include "render_diagnostics.hpp"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <lodepng.h>
#include <set>
#include <algorithm>
#include <cstdio>
namespace {
struct Capture final : bgfx::CallbackI {
    std::filesystem::path output;
    std::atomic<int> result = 0;
    void fatal(const char *, uint16_t, bgfx::Fatal::Enum, const char *message) override {
        std::cerr << message << '\n';
        std::abort();
    }
    void traceVargs(const char *, uint16_t, const char *, va_list) override {}
    void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override {
        return 0;
    }
    bool cacheRead(uint64_t, void *, uint32_t) override {
        return false;
    }
    void cacheWrite(uint64_t, const void *, uint32_t) override {}
    void screenShot(const char *, uint32_t w, uint32_t h, uint32_t pitch, bgfx::TextureFormat::Enum format,
                    const void *data, uint32_t size, bool flip) override {
        try {
            if (uint64_t(pitch) * h > size || pitch < w * 4 || w > 8192 || h > 8192)
                throw std::runtime_error("Invalid capture buffer");
            if (format != bgfx::TextureFormat::BGRA8 && format != bgfx::TextureFormat::RGBA8)
                throw std::runtime_error("Unsupported capture format");
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            auto *src = static_cast<const uint8_t *>(data);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    auto a = (flip ? h - 1 - y : y) * pitch + x * 4, b = (y * w + x) * 4;
                    rgba[b] = src[a + (format == bgfx::TextureFormat::BGRA8 ? 2 : 0)];
                    rgba[b + 1] = src[a + 1];
                    rgba[b + 2] = src[a + (format == bgfx::TextureFormat::BGRA8 ? 0 : 2)];
                    rgba[b + 3] = 255;
                }
            std::vector<uint8_t> png;
            auto code = lodepng::encode(png, rgba, w, h);
            std::ofstream file(output, std::ios::binary);
            file.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
            file.flush();
            result.store(code == 0 && file ? 1 : -1);
        } catch (...) {
            result.store(-1);
        }
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 4 || argc > 7) {
            std::cerr << "gm_scene_capture SCENE.gmscene MODULE.dll OUTPUT.png [INPUT.json] [WIDTH HEIGHT]\n";
            return 2;
        }
        auto registry = std::make_shared<gm::GameRegistry>();
        registry->load(argv[2]);
        gm::useGameRegistry(registry);
        gm::Json profile = gm::Json::object();
        gm::Json setup = gm::Json::array();
        if (argc >= 5 && std::string(argv[4]) != "-") {
            auto input = gm::readJson(argv[4]);
            if (input.is_object())
                profile = input;
            setup = input.is_array() ? input : input.value("setup", gm::Json::array());
        }
        // Diagnostic overrides affect only this loaded copy, never the project or compiled file.
        auto asset = gm::loadScene(argv[1]);
        for (const auto &replacement : profile.value("shaderOverrides", gm::Json::array())) {
            auto &shader = asset.rendering.shaders.at(replacement.at("shader").get<size_t>());
            uint32_t backend = replacement.value("backend", 0u);
            auto file = std::filesystem::path(replacement.at("fragment").get<std::string>());
            if (file.is_relative())
                file = std::filesystem::absolute(argv[4]).parent_path() / file;
            std::ifstream input(file, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read diagnostic shader");
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
            bool replaced = false;
            for (auto &variant : shader.variants)
                if (variant.backend == backend) {
                    variant.fragment = bytes;
                    replaced = true;
                }
            if (!replaced)
                throw std::runtime_error("Diagnostic shader backend missing");
        }
        const auto overrides = profile.value("materialOverrides", gm::Json::object());
        for (const auto &[name, values] : overrides.items()) {
            if (!values.is_array() || values.size() != 4)
                throw std::runtime_error("Material override requires four numbers/nulls");
            for (auto &material : asset.rendering.materials)
                for (auto &parameter : material.parameters)
                    if (parameter.name == name)
                        for (size_t i = 0; i < 4; ++i)
                            if (!values[i].is_null())
                                parameter.value[i] = values[i].get<float>();
        }
        gm::validateRenderAssets(asset.rendering);
        gm::SceneRuntime scene(std::move(asset), std::filesystem::path(argv[1]).parent_path());
        scene.tick(1.f / 60, {});
        for (auto &request : setup) {
            auto response = gm::runtimeCommand(scene, request);
            if (!response.value("ok", false))
                throw std::runtime_error(response.dump());
        }
        const int samples = profile.value("samples", 0), warmup = profile.value("warmup", 60);
        if (samples < 0 || samples > 10000 || warmup < 0 || warmup > 10000)
            throw std::runtime_error("Invalid benchmark length");
        gm::SceneInput benchmarkInput;
        for (auto key : profile.value("keys", gm::Json::array())) {
            int code = key.get<int>();
            if (code < 0 || code > 255)
                throw std::runtime_error("Invalid benchmark key");
            benchmarkInput.pointer.keys[code / 32] |= 1u << (code % 32);
        }
        int width = argc == 7 ? std::stoi(argv[5]) : int(scene.asset().width),
            height = argc == 7 ? std::stoi(argv[6]) : int(scene.asset().height);
        if (width < 64 || height < 64 || width > 4096 || height > 4096)
            throw std::runtime_error("Capture dimensions must be 64..4096");
        Capture callback;
        callback.output = std::filesystem::path(argv[3]);
        std::filesystem::create_directories(std::filesystem::absolute(callback.output).parent_path());
        auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = instance;
        cls.lpszClassName = L"GMSceneCapture";
        RegisterClassW(&cls);
        HWND window = CreateWindowW(cls.lpszClassName, L"Scene render QA", WS_OVERLAPPEDWINDOW, 0, 0, width,
                                    height, nullptr, nullptr, instance, nullptr);
        if (!window)
            throw std::runtime_error("Cannot create render surface");
        {
            gm::Renderer renderer(window, uint32_t(width), uint32_t(height), &callback, false);
            gm::SceneView view(renderer);
            if (samples) {
                std::vector<double> simulation, frontend, gpu, wall;
                std::set<uint64_t> gpuFrames;
                for (int i = 0; i < warmup + samples; ++i) {
                    auto start = std::chrono::steady_clock::now();
                    scene.tick(1.f / 60, benchmarkInput);
                    renderer.begin(width, height);
                    view.render(scene, {0, 0, float(width), float(height)});
                    renderer.end();
                    if (i >= warmup) {
                        simulation.push_back(scene.stats().simulationMs);
                        frontend.push_back(renderer.stats().frontendMs);
                        wall.push_back(std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - start)
                                           .count());
                        if (renderer.stats().gpuMs >= 0 && gpuFrames.insert(renderer.stats().gpuFrame).second)
                            gpu.push_back(renderer.stats().gpuMs);
                    }
                }
                auto distribution = [](std::vector<double> values) -> gm::Json {
                    if (values.empty())
                        return nullptr;
                    std::sort(values.begin(), values.end());
                    return {{"samples", values.size()},
                            {"p50Ms", values[values.size() / 2]},
                            {"p95Ms", values[size_t((values.size() - 1) * .95)]},
                            {"maxMs", values.back()}};
                };
                gm::writeJsonAtomic(std::filesystem::path(argv[3]).concat(".benchmark.json"),
                                    {{"resolution", {width, height}},
                                     {"vsync", false},
                                     {"fixedDt", 1. / 60},
                                     {"warmup", warmup},
                                     {"requestedSamples", samples},
                                     {"vendorId", bgfx::getCaps()->vendorId},
                                     {"deviceId", bgfx::getCaps()->deviceId},
                                     {"simulation", distribution(simulation)},
                                     {"frontend", distribution(frontend)},
                                     {"gpu", distribution(gpu)},
                                     {"frameWall", distribution(wall)},
                                     {"gpuNote", "Delayed unique GPU samples; not paired with CPU frame "
                                                 "timings. Hidden surface; not a displayed FPS measurement."},
                                     {"stats", gm::renderDiagnostics(renderer.stats())}});
            }
            const auto sequence = profile.value("sequence", gm::Json::object());
            const int sequenceFrames = sequence.value("frames", 0);
            if (sequenceFrames < 0 || sequenceFrames > 600)
                throw std::runtime_error("Sequence length must be 0..600");
            for (int i = 0; i < sequenceFrames; ++i) {
                for (const auto &item : sequence.value("commands", gm::Json::array()))
                    if (item.at("frame") == i) {
                        auto response = gm::runtimeCommand(scene, item.at("request"));
                        if (!response.value("ok", false))
                            throw std::runtime_error(response.dump());
                    }
                scene.tick(1.f / 60, benchmarkInput);
                char name[40];
                std::snprintf(name, sizeof(name), "frame-%04d.png", i);
                callback.output = std::filesystem::path(argv[3]).parent_path() /
                                  std::filesystem::path(argv[3]).stem() / name;
                std::filesystem::create_directories(callback.output.parent_path());
                callback.result = 0;
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "sequence");
                renderer.begin(width, height);
                view.render(scene, {0, 0, float(width), float(height)});
                renderer.end();
                // Drain readback without rendering extra temporal samples or advancing simulation.
                for (int wait = 0; wait < 120 && !callback.result.load(); ++wait) {
                    bgfx::frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (callback.result.load() != 1)
                    throw std::runtime_error("Sequence GPU readback failed");
            }
            callback.output = std::filesystem::path(argv[3]);
            callback.result = 0;
            for (int i = 0; i < 5; ++i) {
                renderer.begin(width, height);
                view.render(scene, {0, 0, float(width), float(height)});
                renderer.end();
            }
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "capture");
            for (int i = 0; i < 120 && !callback.result.load(); ++i) {
                renderer.begin(width, height);
                view.render(scene, {0, 0, float(width), float(height)});
                renderer.end();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            gm::writeJsonAtomic(std::filesystem::path(argv[3]).concat(".stats.json"),
                                gm::renderDiagnostics(renderer.stats()));
            std::cout << "canvas=" << scene.canvas().size() << " quads=" << renderer.stats().quads
                      << " draws=" << renderer.stats().drawCalls
                      << " textUploads=" << renderer.stats().textUploads
                      << " textEntries=" << renderer.stats().textCacheEntries
                      << " textBytes=" << renderer.stats().textTextureBytes
                      << " simulationMs=" << scene.stats().simulationMs << '\n';
        }
        DestroyWindow(window);
        if (callback.result.load() != 1)
            throw std::runtime_error("GPU screenshot failed/timed out");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
