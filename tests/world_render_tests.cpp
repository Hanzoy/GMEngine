#include "project.hpp"
#include "render_compiler.hpp"
#include "environment_cooker.hpp"
#include "scene_view.hpp"
#include "render_diagnostics.hpp"
#include <windows.h>
#include <dbghelp.h>
#include "fixtures/render/vs_custom_dxbc.bin.h"
#include "fixtures/render/fs_custom_dxbc.bin.h"
#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <bit>
#include <lodepng.h>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Capture : bgfx::CallbackI {
    std::atomic<bool> ready = false;
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    void fatal(const char *, uint16_t, bgfx::Fatal::Enum, const char *text) override {
        std::cerr << text;
        std::abort();
    }
    void traceVargs(const char *, uint16_t, const char *format, va_list args) override {
        vfprintf(stderr, format, args);
    }
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
                    const void *data, uint32_t, bool flip) override {
        width = w;
        height = h;
        pixels.resize(size_t(w) * h * 4);
        auto src = static_cast<const uint8_t *>(data);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                auto i = (flip ? h - 1 - y : y) * pitch + x * 4, j = (y * w + x) * 4;
                pixels[j] = src[i + (format == bgfx::TextureFormat::BGRA8 ? 2 : 0)];
                pixels[j + 1] = src[i + 1];
                pixels[j + 2] = src[i + (format == bgfx::TextureFormat::BGRA8 ? 0 : 2)];
                pixels[j + 3] = 255;
            }
        ready = true;
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void *, uint32_t) override {}
};
} // namespace
int main(int argc, char **argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *e) -> LONG {
        auto process = GetCurrentProcess();
        SymInitialize(process, nullptr, TRUE);
        CONTEXT context = *e->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        for (int i = 0; i < 20; ++i) {
            char buffer[sizeof(SYMBOL_INFO) + 1024]{};
            auto symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 1023;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                std::cerr << symbol->Name << " + " << displacement << '\n';
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
    try {
        auto output = std::filesystem::path(argc > 1 ? argv[1] : "world-render-qa");
        std::filesystem::create_directories(output);
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"GMWorldRenderTest";
        RegisterClassW(&cls);
        auto window = CreateWindowW(cls.lpszClassName, L"GM world renderer contracts", WS_OVERLAPPEDWINDOW, 0,
                                    0, 640, 480, nullptr, nullptr, cls.hInstance, nullptr);
        check(window, "Window unavailable");
        {
            Capture capture;
            gm::Renderer renderer(window, 640, 480, &capture, false);
            gm::WorldRenderer world(renderer);
            gm::RenderAssets assets;
            assets.meshes.push_back(gm::primitiveMesh("builtin:cube"));
            gm::CookedMaterial red, green;
            red.parameters = {{"u_baseColor", {1, 0, 0, 1}}, {"u_unlit", {1, 0, 0, 0}}};
            green.parameters = {{"u_baseColor", {0, 1, 0, 1}}, {"u_unlit", {1, 0, 0, 0}}};
            assets.materials = {red, green};
            gm::RenderFrame frame;
            frame.cameraEnabled = true;
            frame.camera = gm::local3D({0, 0, -5});
            frame.meshes = {{1, 0, 0, gm::local3D({0, 0, 0, 0, 0, 0, 2, 2, 2})},
                            {2, 0, 1, gm::local3D({0, 0, 2, 0, 0, 0, 2, 2, 2})}};
            gm::Rect viewport{0, 0, 640, 480};
            auto draw = [&] {
                check(world.begin(assets, frame, viewport, 0xff000000), "World layer inactive");
                world.finish();
                renderer.rect({8, 8, 60, 20}, 0xffffffff);
            };
            auto snapshot = [&](auto draw) {
                capture.ready = false;
                for (int i = 0; i < 6; ++i) {
                    renderer.begin(640, 480);
                    draw();
                    renderer.end();
                }
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "test");
                for (int i = 0; i < 100 && !capture.ready; ++i) {
                    renderer.begin(640, 480);
                    draw();
                    renderer.end();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                check(capture.ready, "GPU readback timeout");
                return capture.pixels;
            };
            auto pixels = snapshot(draw);
            auto pixel = [&](int x, int y, int c) { return pixels[(y * 640 + x) * 4 + c]; };
            lodepng::encode((output / "depth.png").string(), pixels, 640, 480);
            check(pixel(320, 240, 0) > 245 && pixel(320, 240, 1) < 5, "Depth/cull/material regression");
            check(pixel(10, 10, 0) == 255, "HUD ordering regression");
            std::reverse(frame.meshes.begin(), frame.meshes.end());
            check(snapshot(draw) == pixels, "Depth depends on submission order");
            // A one-sided plane must face the camera; closed cubes alone cannot detect reversed culling.
            world.clear();
            assets.meshes.push_back(gm::primitiveMesh("builtin:plane"));
            frame.meshes = {{1, 1, 0, gm::local3D({0, 0, 0, -90, 0, 0, 2, 2, 2})}};
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) > 245, "Back-face winding convention");
            assets.materials[0].cull = gm::CullMode::Front;
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) < 5, "Front-face culling convention");
            assets.materials[0].cull = gm::CullMode::Back;
            // Transparent objects must sort back to front and preserve destination color.
            world.clear();
            assets.materials[0].blend = gm::BlendMode::Alpha;
            assets.materials[0].depthWrite = false;
            assets.materials[0].parameters[0].value = {1, 0, 0, .5f};
            frame.meshes = {{1, 0, 0, gm::local3D({0, 0, 0, 0, 0, 0, 2, 2, 2})},
                            {2, 0, 1, gm::local3D({0, 0, 2, 0, 0, 0, 2, 2, 2})}};
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) > 170 && pixel(320, 240, 1) > 170, "Alpha material blending");
            std::reverse(frame.meshes.begin(), frame.meshes.end());
            check(snapshot(draw) == pixels, "Transparent ordering regression");
            // Custom shader reflection binds arbitrary parameters and resets absent values per material.
            world.clear();
            assets.shaders = {gm::CookedShader{
                {gm::ShaderVariant{0,
                                   {std::begin(vs_custom_dxbc), std::end(vs_custom_dxbc)},
                                   {std::begin(fs_custom_dxbc), std::end(fs_custom_dxbc)}}}}};
            gm::CookedMaterial custom;
            custom.shader = 0;
            custom.parameters = {{"u_customColor", {0, 0, 1, 1}}};
            assets.materials = {custom};
            frame.meshes = {{1, 0, 0, gm::identity3D()}};
            pixels = snapshot(draw);
            check(pixel(320, 240, 2) > 245 && pixel(320, 240, 0) < 5, "Custom vec4 shader binding");
            assets.materials[0].parameters.push_back({"u_optionalColor", {1, 0, 0, 0}});
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) > 245, "Custom optional parameter update");
            assets.materials[0].parameters.pop_back();
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) < 5, "Custom material inherited stale uniform");
            assets.materials[0].parameters.push_back({"u_missing", {1, 1, 1, 1}});
            bool rejected = false;
            renderer.begin(640, 480);
            try {
                draw();
            } catch (const std::exception &) {
                rejected = true;
            }
            renderer.end();
            check(rejected, "Missing custom shader parameter was ignored");
            world.clear();
            assets.shaders.clear();
            // HDR emission and bloom must light pixels beyond the geometry, without touching HUD.
            world.clear();
            assets.materials = {red};
            assets.materials[0].parameters.push_back({"u_emission", {6, 2, 0, 1}});
            frame.meshes = {{1, 0, 0, gm::identity3D()}};
            frame.post = {};
            frame.post.fxaa = 0;
            frame.post.bloom = 0;
            auto noBloom = snapshot(draw);
            frame.post.bloom = 1;
            frame.post.bloomIntensity = 1;
            frame.post.bloomRadius = 2;
            auto bloom = snapshot(draw);
            size_t brighter = 0;
            for (size_t i = 0; i < bloom.size(); i += 4)
                if (noBloom[i] < 5 && bloom[i] > 10)
                    ++brighter;
            check(brighter > 50, "Bloom absent outside silhouette");
            check(bloom[(10 * 640 + 10) * 4] == 255, "Post processing affected HUD");
            lodepng::encode((output / "bloom.png").string(), bloom, 640, 480);
            frame.post.fxaa = 1;
            check(snapshot(draw) != bloom, "FXAA produced no edge change");
            // Mipmapped compressed and fallback texture paths both render the same intended colors.
            gm::UiImage image{32, 32, std::vector<uint8_t>(4096)};
            for (size_t i = 0; i < image.rgba.size(); i += 4) {
                image.rgba[i] = 180;
                image.rgba[i + 1] = 90;
                image.rgba[i + 2] = 45;
                image.rgba[i + 3] = 255;
            }
            assets.textures = {gm::cookTexture(image, {})};
            assets.materials = {red};
            assets.materials[0].parameters[0].value = {1, 1, 1, 1};
            assets.materials[0].samplers = {{"s_baseColor", 0, 0}};
            frame.post = {0};
            world.clear();
            pixels = snapshot(draw);
            check(renderer.stats().compressedTextures == 1 && renderer.stats().mipLevels == 6,
                  "Compressed mip chain not selected");
            check(std::abs(int(pixel(320, 240, 0)) - 180) < 12 && std::abs(int(pixel(320, 240, 1)) - 90) < 12,
                  "Compressed sRGB sample/color conversion");
            auto compressedStats = gm::renderDiagnostics(renderer.stats());
            gm::TextureSettings rgba;
            rgba.formats = {gm::TextureFormat::Rgba8};
            assets.textures = {gm::cookTexture(image, rgba)};
            world.clear();
            pixels = snapshot(draw);
            check(renderer.stats().compressedTextures == 0 &&
                      renderer.stats().materialTextureBytes >
                          compressedStats["materialTextureBytes"].get<uint64_t>() * 3,
                  "RGBA fallback allocation");
            gm::UiImage odd{5, 3, std::vector<uint8_t>(60, 255)};
            assets.textures = {gm::cookTexture(odd, {})};
            world.clear();
            pixels = snapshot(draw);
            check(pixel(320, 240, 0) > 250 && renderer.stats().compressedTextures == 0,
                  "Odd-sized texture fallback");
            // Compare output exactly with instancing disabled/enabled; cull distant objects.
            world.clear();
            assets.textures.clear();
            assets.materials = {red};
            frame.meshes.clear();
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 25; ++x)
                    frame.meshes.push_back(
                        {uint32_t(y * 25 + x), 0, 0,
                         gm::local3D({(x - 12) * .18f, (y - 9.5f) * .18f, 0, 0, 0, 0, .12f, .12f, .12f})});
            for (int x = 0; x < 1000; ++x)
                frame.meshes.push_back({uint32_t(500 + x), 0, 0, gm::local3D({1000.f + x, 0, 0})});
            world.instancing(false);
            auto direct = snapshot(draw);
            auto directStats = gm::renderDiagnostics(renderer.stats());
            world.instancing(true);
            auto batched = snapshot(draw);
            auto batchStats = gm::renderDiagnostics(renderer.stats());
            lodepng::encode((output / "direct.png").string(), direct, 640, 480);
            lodepng::encode((output / "instanced.png").string(), batched, 640, 480);
            size_t differences = 0;
            int maxDifference = 0;
            for (size_t i = 0; i < direct.size(); ++i) {
                int delta = std::abs(int(direct[i]) - int(batched[i]));
                differences += delta > 0;
                maxDifference = std::max(maxDifference, delta);
            }
            std::cerr << "Instance pixel differences: " << differences << " max " << maxDifference << "\n";
            check(direct == batched, "Instancing changed output");
            check(renderer.stats().visibleMeshes == 500 && renderer.stats().meshDraws == 1 &&
                      renderer.stats().instances == 500,
                  "Culling/instancing counters");
            check(directStats["meshDraws"] == 500, "Reference draw count");
            lodepng::encode((output / "instances.png").string(), batched, 640, 480);
            gm::Json timings = gm::Json::array();
            for (bool enabled : {false, true}) {
                world.instancing(enabled);
                std::vector<double> samples;
                for (int i = 0; i < 150; ++i) {
                    renderer.begin(640, 480);
                    auto start = std::chrono::steady_clock::now();
                    draw();
                    auto ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                            .count();
                    renderer.end();
                    if (i >= 30)
                        samples.push_back(ms);
                }
                std::sort(samples.begin(), samples.end());
                timings.push_back({{"instanced", enabled},
                                   {"submitMedianMs", samples[samples.size() / 2]},
                                   {"submitP95Ms", samples[size_t(samples.size() * .95)]},
                                   {"stats", gm::renderDiagnostics(renderer.stats())}});
            }
            // Rotated/nonuniform scaled lighting must use the same normal basis in both vertex paths.
            world.clear();
            assets.materials[0].parameters.pop_back();
            frame.light.intensity = 3;
            frame.light.directionZ = 1;
            for (auto &d : frame.meshes) {
                auto t = d.transform;
                d.transform = gm::multiply3D(t, gm::local3D({0, 0, 0, 25, 35, 15, 1, 1.5f, .7f}));
            }
            world.instancing(false);
            auto litDirect = snapshot(draw);
            world.instancing(true);
            auto litInstances = snapshot(draw);
            int normalDifference = 0;
            for (size_t i = 0; i < litDirect.size(); ++i)
                normalDifference =
                    std::max(normalDifference, std::abs(int(litDirect[i]) - int(litInstances[i])));
            check(normalDifference <= 2, "Instanced lighting/normal basis mismatch");
            gm::Json postTimings = gm::Json::array();
            viewport = {0, 0, 1920, 1080};
            assets.materials[0].parameters.push_back({"u_emission", {2, .3f, .1f, 1}});
            for (int mode = 0; mode < 4; ++mode) {
                frame.post = {};
                frame.post.enabled = mode != 0;
                frame.post.bloom = mode >= 2;
                frame.post.renderScale = mode == 3 ? .5f : 1.f;
                std::vector<double> cpu, gpu;
                for (int i = 0; i < 110; ++i) {
                    renderer.begin(1920, 1080);
                    draw();
                    renderer.end();
                    if (i >= 30) {
                        cpu.push_back(renderer.stats().frontendMs);
                        if (renderer.stats().gpuMs >= 0)
                            gpu.push_back(renderer.stats().gpuMs);
                    }
                }
                std::sort(cpu.begin(), cpu.end());
                std::sort(gpu.begin(), gpu.end());
                postTimings.push_back(
                    {{"mode", mode},
                     {"frontendMedianMs", cpu[cpu.size() / 2]},
                     {"gpuMedianMs", gpu.empty() ? gm::Json(nullptr) : gm::Json(gpu[gpu.size() / 2])},
                     {"stats", gm::renderDiagnostics(renderer.stats())}});
            }
            #include "lighting_gpu_tests.inl"
            // Resize and repeated resource lifetimes must not retain targets from old worlds.
            for (int n = 0; n < 12; ++n) {
                renderer.begin(640, 480);
                check(world.begin(assets, frame, {20, 30, float(240 + n * 8), 180}, 0xff000000),
                      "Resized target");
                world.finish();
                renderer.end();
                world.clear();
            }
            renderer.begin(640, 480);
            renderer.end();
            check(renderer.stats().targetBytes == 0 && renderer.stats().meshBytes == 0,
                  "World resource accounting leak");
            // Scene restoration keeps the SceneRuntime address; GPU banks must still refresh.
            auto scene = gm::compileScene(gm::newScene(), output);
            auto registry = gm::gameRegistry();
            auto component = [&](const auto &value) {
                auto type = gm::sdk::id(std::decay_t<decltype(value)>::gm_name);
                return gm::ComponentValue{type, 1, registry->encode(type, &value)};
            };
            scene.entities = {
                {"camera",
                 gm::NoIndex,
                 {component(gm::sdk::Transform3D{0, 0, -5}), component(gm::sdk::Camera3D{})}},
                {"mesh",
                 gm::NoIndex,
                 {component(gm::sdk::Transform3D{}), component(gm::sdk::MeshRenderer{0, 0, 1})}}};
            scene.rendering.meshes = {gm::primitiveMesh("builtin:cube")};
            scene.rendering.materials = {red};
            scene.rendering.materials[0].parameters[0].value = {1, 1, 1, 1};
            scene.rendering.materials[0].samplers = {{"s_baseColor", 0, 0}};
            gm::TextureSettings settings;
            settings.formats = {gm::TextureFormat::Rgba8};
            scene.rendering.textures = {gm::cookTexture({1, 1, {255, 0, 0, 255}}, settings)};
            gm::SceneRuntime runtime(scene);
            gm::SceneView sceneView(renderer);
            auto sceneDraw = [&] { sceneView.render(runtime, {0, 0, 640, 480}); };
            pixels = snapshot(sceneDraw);
            check(pixel(320, 240, 0) > 245, "SceneView 3D render");
            auto gameState = gm::encodeScene(runtime.snapshot());
            gm::ViewCamera3D editorCamera;
            editorCamera.transform = gm::local3D(gm::sdk::Transform3D{50, 0, -5});
            pixels = snapshot([&] { sceneView.render(runtime, {0, 0, 640, 480}, false, &editorCamera); });
            check(pixel(320,240,0)<200, "Editor camera override did not change rendered viewpoint");
            check(gm::encodeScene(runtime.snapshot())==gameState, "Editor view changed game World");
            pixels=snapshot(sceneDraw);
            check(pixel(320,240,0)>245, "Game camera not restored after independent Scene view");
            auto noCamera=scene;
            noCamera.entities.erase(noCamera.entities.begin());
            runtime.restore(noCamera);
            editorCamera.transform=gm::local3D(gm::sdk::Transform3D{0,0,-5});
            pixels=snapshot([&] {sceneView.render(runtime,{0,0,640,480},false,&editorCamera);});
            check(pixel(320,240,0)>245, "Editor cannot render a scene without Camera3D");
            scene.rendering.textures = {gm::cookTexture({1, 1, {0, 255, 0, 255}}, settings)};
            runtime.restore(scene);
            pixels = snapshot(sceneDraw);
            check(pixel(320, 240, 1) > 245 && pixel(320, 240, 0) < 5,
                  "Scene restore reused stale GPU texture bank");
            // Inject a screen-space Canvas fixture: its color and glyph edges must
            // survive HDR exposure, bloom, FXAA and a half-resolution world target.
            scene.width=640;scene.height=480;scene.hud.nodes[0].bounds.w=640;scene.hud.nodes[0].bounds.h=480;
            auto canvasPixels=[&](bool post){
                auto fixture=scene;
                gm::sdk::PostProcess fx;fx.enabled=post;fx.exposure=4;fx.bloom=1;fx.renderScale=.5f;
                fixture.entities.push_back({"post",gm::NoIndex,{component(fx)}});
                runtime.restore(fixture);
                auto& commands=const_cast<std::vector<gm::sdk::CanvasCommand>&>(runtime.canvas());
                gm::sdk::CanvasCommand panel;panel.x=20;panel.y=20;panel.w=280;panel.h=80;panel.color=0xff403020;commands.push_back(panel);
                gm::sdk::CanvasCommand label;label.kind=gm::sdk::CanvasKind::Text;label.x=28;label.y=35;label.w=260;label.size=22;label.color=0xffd0e0f0;std::snprintf(label.text,sizeof label.text,"HUD / Native pixels");commands.push_back(label);
                return snapshot(sceneDraw);
            };
            auto canvasDirect=canvasPixels(false),canvasPost=canvasPixels(true);
            for(int y=22;y<98;++y)for(int x=22;x<298;++x)for(int channel=0;channel<3;++channel)
                check(canvasDirect[(y*640+x)*4+channel]==canvasPost[(y*640+x)*4+channel],"Canvas was exposed, bloomed or scaled with the world");
            sceneView.clear();
            gm::writeJsonAtomic(output / "performance.json", {{"vsync", false},
                                                              {"vendorId", bgfx::getCaps()->vendorId},
                                                              {"deviceId", bgfx::getCaps()->deviceId},
                                                              {"reference", directStats},
                                                              {"instanced", batchStats},
                                                              {"compressed", compressedStats},
                                                              {"timings", timings},
                                                              {"postResolution", {1920, 1080}},
                                                              {"postTimings", postTimings}});
        }
        DestroyWindow(window);
        std::cout << "3D GPU contracts: depth, alpha, bloom, FXAA, compression, instancing, culling, resize "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
