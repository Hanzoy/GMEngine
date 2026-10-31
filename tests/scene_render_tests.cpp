#include "project.hpp"
#include "scene_view.hpp"
#include "render_diagnostics.hpp"
#include <windows.h>
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
    try {
        const auto output = argc > 1 ? std::filesystem::path(argv[1])
                                     : std::filesystem::temp_directory_path() / "gm-scene-render-test";
        std::filesystem::create_directories(output);
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"GMSceneRenderTest";
        RegisterClassW(&cls);
        auto window = CreateWindowW(cls.lpszClassName, L"GM scene rendering contract", WS_OVERLAPPEDWINDOW, 0,
                                    0, 960, 600, nullptr, nullptr, cls.hInstance, nullptr);
        check(window, "Window unavailable");
        {
            Capture capture;
            gm::Renderer renderer(window, 960, 600, &capture);
            auto snapshot = [&](auto draw) {
                capture.ready = false;
                for (int i = 0; i < 4; ++i) {
                    renderer.begin(960, 600);
                    draw();
                    renderer.end();
                }
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "test");
                for (int i = 0; i < 100 && !capture.ready; ++i) {
                    renderer.begin(960, 600);
                    draw();
                    renderer.end();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                check(capture.ready, "GPU readback timeout");
                return capture.pixels;
            };
            gm::UiImage red{2, 2, std::vector<uint8_t>(16)}, green = red;
            for (size_t i = 0; i < 16; i += 4) {
                red.rgba[i] = 255;
                red.rgba[i + 3] = 255;
                green.rgba[i + 1] = 255;
                green.rgba[i + 3] = 255;
            }
            auto r = renderer.uploadImage(red), g = renderer.uploadImage(green);
            auto duplicate = renderer.uploadImage(red);
            check(duplicate.id == r.id, "Identical content not shared");
            auto drawImages = [&] {
                renderer.rect({0, 0, 960, 600}, 0xff000000);
                renderer.image({20, 20, 120, 100}, r, gm::ImageFit::Stretch);
                renderer.image({140, 20, 120, 100}, g, gm::ImageFit::Stretch);
                renderer.clip({50, 150, 100, 100});
                renderer.image({0, 130, 250, 150}, r, gm::ImageFit::Cover);
                renderer.unclip();
            };
            auto direct = snapshot(drawImages);
            check(renderer.stats().imageTextures == 1 && renderer.stats().imageAtlasPages == 1,
                  "Small images did not share one physical atlas");
            auto pixel = [&](int x, int y, int c) { return direct[(y * 960 + x) * 4 + c]; };
            check(pixel(21, 21, 0) > 250 && pixel(139, 50, 1) < 3 && pixel(141, 50, 1) > 250,
                  "Atlas edge extrusion/UV color regression");
            check(pixel(49, 170, 0) < 3 && pixel(50, 170, 0) > 250 && pixel(150, 170, 0) < 3,
                  "Image clipping regression");
            renderer.begin(960, 600);
            auto cache = renderer.buildGeometry(drawImages);
            renderer.end();
            auto cached = snapshot([&] { check(renderer.drawGeometry(cache), "Image cache invalid"); });
            check(direct == cached, "Cached UI changed pixels");
            renderer.releaseGeometry(cache);
            renderer.begin(960, 600);
            auto changed = red;
            changed.rgba[1] = 45;
            auto changedHandle = renderer.uploadImage(changed);
            renderer.end();
            check(renderer.stats().imageUploads == 1 && changedHandle.id != r.id,
                  "Changing pixels must upload only the new content");
            renderer.releaseImage(changedHandle);
            // Keep red live while transient regions share its page. Expired metadata
            // must not accumulate until the 4096 logical-handle budget is exhausted.
            for (uint32_t i = 0; i < 4200; ++i) {
                changed.rgba[0] = uint8_t(i);
                changed.rgba[1] = uint8_t(i >> 8);
                auto temporary = renderer.uploadImage(changed);
                renderer.releaseImage(temporary);
                if (i % 32 == 0)
                    renderer.trimImages(true);
            }
            renderer.trimImages(true);
            check(snapshot(drawImages) == direct, "Reclaiming idle atlas entries damaged a live image");
            renderer.releaseImage(r);
            renderer.releaseImage(duplicate);
            renderer.releaseImage(g);
            renderer.trimImages(true);
            // 40,000 buildings: only a viewport-sized fraction should enter the front end.
            gm::useGameRegistry(std::make_shared<gm::GameRegistry>());
            auto source = gm::newScene();
            source["width"] = 960;
            source["height"] = 600;
            auto comp = [](const char *name, gm::Json fields) {
                auto &desc = gm::sceneComponent(name);
                for (auto &f : desc.fields)
                    if (!fields.contains(f.key))
                        fields[f.key] = f.initial;
                return gm::Json{{"type", name}, {"version", desc.version}, {"fields", fields}};
            };
            source["entities"].push_back(
                {{"id", "building"},
                 {"name", "Building"},
                 {"parent", nullptr},
                 {"components",
                  gm::Json::array(
                      {comp("Transform2D", {{"x", 0}, {"y", 0}}),
                       comp("SpriteRenderer", {{"width", 24}, {"height", 24}, {"tint", "#4399DD"}})})}});
            auto asset = gm::compileScene(source, {});
            auto building = asset.entities[0];
            asset.entities.clear();
            for (uint32_t i = 0; i < 40000; ++i) {
                auto e = building;
                e.id = "b" + std::to_string(i);
                e.components[0].fields = {std::bit_cast<uint32_t>(float(i % 200) * 32),
                                          std::bit_cast<uint32_t>(float(i / 200) * 32)};
                asset.entities.push_back(std::move(e));
            }
            gm::SceneRuntime world(asset);
            gm::SceneView view(renderer);
            auto render = [&] { view.render(world, {0, 0, 960, 600}, false); };
            auto optimized = snapshot(render);
            check(renderer.stats().totalSprites == 40000 && renderer.stats().spriteCandidates < 2000 &&
                      renderer.stats().visibleSprites == 570,
                  "Large-world visibility count regression");
            check(renderer.stats().cachedDraws > 0 && renderer.stats().vertexUploadBytes <= 92,
                  "Static world reuploaded geometry");
            auto reference = [&] {
                renderer.clip({0, 0, 960, 600});
                renderer.rect({0, 0, 960, 600}, world.asset().background);
                for (auto &s : world.sprites()) {
                    const auto &t = s.transform;
                    renderer.sprite({t.x, t.y, t.x + 24, t.y, t.x + 24, t.y + 24, t.x, t.y + 24}, {}, s.tint);
                }
                renderer.unclip();
            };
            check(snapshot(reference) == optimized, "Visibility/cached world changed pixels");
            auto benchmark = [&](const char *name, auto draw) {
                std::vector<double> samples;
                for (int i = 0; i < 80; ++i) {
                    renderer.begin(960, 600);
                    auto start = std::chrono::steady_clock::now();
                    draw();
                    auto end = std::chrono::steady_clock::now();
                    renderer.end();
                    if (i >= 20)
                        samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
                }
                std::sort(samples.begin(), samples.end());
                std::cout << name << ",frontend_us=" << samples[samples.size() / 2]
                          << ",draws=" << renderer.stats().drawCalls
                          << ",vertex_bytes=" << renderer.stats().vertexUploadBytes << '\n';
                gm::writeJsonAtomic(output / (std::string(name) + ".json"),
                                    gm::renderDiagnostics(renderer.stats()));
            };
            benchmark("40000_full_scan_reference", reference);
            benchmark("40000_spatial_cached", render);
            // Nonidentity camera/view transform must survive the static GPU path.
            gm::Json cameraFields = gm::Json::array(
                {comp("Transform2D", {{"x", 64}, {"y", 32}}), comp("Camera2D", {{"zoom", 2}})});
            auto cameraSource = gm::newScene();
            cameraSource["entities"].push_back(
                {{"id", "camera"}, {"name", "Camera"}, {"parent", nullptr}, {"components", cameraFields}});
            auto cameraAsset = gm::compileScene(cameraSource, {});
            asset.entities.push_back(cameraAsset.entities[0]);
            world.restore(asset);
            auto zoomed = snapshot(render);
            auto referenceZoom = [&] {
                renderer.clip({0, 0, 960, 600});
                renderer.rect({0, 0, 960, 600}, world.asset().background);
                for (auto &s : world.sprites()) {
                    float x = (s.transform.x - 64) * 2, y = (s.transform.y - 32) * 2;
                    renderer.sprite({x, y, x + 48, y, x + 48, y + 48, x, y + 48}, {}, s.tint);
                }
                renderer.unclip();
            };
            check(snapshot(referenceZoom) == zoomed, "Cached geometry ignored camera transform");
            auto cameraEntity = world.entity(uint32_t(asset.entities.size() - 1));
            world.set(cameraEntity, gm::sdk::id(gm::sdk::Transform2D::gm_name),
                      std::array<uint32_t, 2>{std::bit_cast<uint32_t>(65.f), std::bit_cast<uint32_t>(32.f)});
            auto panned = snapshot(render);
            check(panned != zoomed && renderer.stats().vertexUploadBytes <= 92,
                  "Small camera pan should reuse world-space GPU geometry");
            world.set(cameraEntity, gm::sdk::id(gm::sdk::Transform2D::gm_name),
                      std::array<uint32_t, 2>{std::bit_cast<uint32_t>(64.f), std::bit_cast<uint32_t>(32.f)});
            auto entity = world.entity(0);
            world.set(entity, gm::sdk::id(gm::sdk::Transform2D::gm_name),
                      std::array<uint32_t, 2>{std::bit_cast<uint32_t>(70.f), std::bit_cast<uint32_t>(40.f)});
            auto moved = snapshot(render);
            check(moved != zoomed && moved == snapshot(referenceZoom),
                  "Transform mutation did not invalidate visible geometry");
            gm::sdk::SpriteRenderer appearance;
            auto spriteType = gm::sdk::id(gm::sdk::SpriteRenderer::gm_name);
            gm::gameRegistry()->decode(spriteType, world.component(0, spriteType), &appearance);
            appearance.order = 10;
            appearance.tint = 0xff3333ff;
            world.set(entity, spriteType, gm::gameRegistry()->encode(spriteType, &appearance));
            auto reordered = snapshot(render);
            check(reordered != moved && reordered == snapshot(referenceZoom),
                  "Painter order/tint cache invalidation");
            gm::sdk::Pose2D pose;
            pose.rotation = 90;
            world.set(cameraEntity, gm::sdk::id(gm::sdk::Pose2D::gm_name),
                      gm::gameRegistry()->encode(gm::sdk::id(gm::sdk::Pose2D::gm_name), &pose));
            auto rotated = snapshot(render);
            auto referenceRotated = [&] {
                renderer.clip({0, 0, 960, 600});
                renderer.rect({0, 0, 960, 600}, world.asset().background);
                const auto camera = world.camera();
                const auto &c = camera.transform;
                float det = c.a * c.d - c.b * c.c;
                for (const auto &s : world.sprites()) {
                    std::array<float, 8> points{};
                    for (int i = 0; i < 4; ++i) {
                        float dx = s.transform.x + ((i == 1 || i == 2) ? 24 : 0) - c.x;
                        float dy = s.transform.y + (i >= 2 ? 24 : 0) - c.y;
                        points[2 * i] = (c.d * dx - c.c * dy) / det * camera.zoom;
                        points[2 * i + 1] = (-c.b * dx + c.a * dy) / det * camera.zoom;
                    }
                    renderer.sprite(points, {}, s.tint);
                }
                renderer.unclip();
            };
            check(rotated == snapshot(referenceRotated), "Rotated camera/world clipping changed pixels");
            std::vector<uint8_t> png;
            check(!lodepng::encode(png, moved, 960, 600), "PNG encoding failed");
            std::ofstream file(output / "large-world.png", std::ios::binary);
            file.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
            std::cout << "PASS: GPU pixel equality for images, cache, 40k scene, camera pan/zoom, transform "
                         "invalidation\n";
        }
        DestroyWindow(window);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
