#include "project.hpp"
#include "render_compiler.hpp"
#include "environment_cooker.hpp"
#include "shadow_math.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <lodepng.h>
#include <bimg/bimg.h>
#include <bx/allocator.h>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    check(bad, "Invalid rendering resource accepted");
}
gm::Json component(const std::string &type, gm::Json values = gm::Json::object()) {
    gm::Json fields = gm::Json::object();
    for (auto &f : gm::sceneComponent(type).fields)
        fields[f.key] = f.initial;
    fields.update(values);
    return {{"type", type}, {"version", 1}, {"fields", fields}};
}
gm::Json entity(const char *id, std::initializer_list<gm::Json> components, gm::Json parent = nullptr) {
    return {{"id", id}, {"name", id}, {"parent", parent}, {"components", gm::Json(components)}};
}
} // namespace
int main(int argc, char **argv) {
    try {
        auto root = std::filesystem::path(argc > 1 ? argv[1] : "render-assets-test");
        std::filesystem::create_directories(root);
        gm::useGameRegistry(std::make_shared<gm::GameRegistry>());
        gm::UiImage colors{2, 1, {0, 0, 0, 255, 255, 255, 255, 255}};
        auto linear = gm::generateMipmaps(colors, false, false),
             srgb = gm::generateMipmaps(colors, true, false);
        check(linear.back().rgba[0] == 128 && std::abs(int(srgb.back().rgba[0]) - 188) <= 1,
              "sRGB mip filtering is not linear light");
        gm::UiImage edge{2, 1, {255, 0, 0, 255, 0, 0, 255, 0}};
        auto transparent = gm::generateMipmaps(edge, true, false);
        check(transparent.back().rgba[0] == 255 && transparent.back().rgba[2] == 0 &&
                  transparent.back().rgba[3] == 128,
              "Transparent mip fringe");
        gm::UiImage odd{5, 3, std::vector<uint8_t>(60, 255)};
        auto oddMips = gm::generateMipmaps(odd, false, false);
        check(oddMips.size() == 3 && oddMips[1].width == 2 && oddMips[1].height == 1 &&
                  oddMips.back().rgba[0] == 255,
              "Odd mip dimensions");
        gm::UiImage normals{2, 1, {255, 128, 128, 255, 128, 128, 255, 255}};
        auto norm = gm::generateMipmaps(normals, false, true).back();
        check(norm.rgba[0] > 210 && norm.rgba[2] > 210, "Normal mip not renormalized");
        gm::UiImage image{32, 32, std::vector<uint8_t>(32 * 32 * 4)};
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                auto i = (y * 32 + x) * 4;
                image.rgba[i] = uint8_t(x * 8);
                image.rgba[i + 1] = uint8_t(y * 8);
                image.rgba[i + 2] = 128;
                image.rgba[i + 3] = 255;
            }
        check(lodepng::encode((root / "gradient.png").string(), image.rgba, 32, 32) == 0,
              "PNG fixture write");
        gm::RenderAssets bank;
        bank.textures.push_back(gm::cookTexture(image, {}));
        check(bank.textures[0].levels == 6 && bank.textures[0].variants.size() == 4,
              "Missing mip/format variants");
        for (auto &v : bank.textures[0].variants)
            check(v.bytes.size() == gm::textureBytes(bank.textures[0], v.format), "Compressed chain size");
        gm::Json quality = gm::Json::array();
        bx::DefaultAllocator allocator;
        for (auto &v : bank.textures[0].variants) {
            if (v.format == gm::TextureFormat::Rgba8)
                continue;
            auto f = v.format == gm::TextureFormat::BC3       ? bimg::TextureFormat::BC3
                     : v.format == gm::TextureFormat::ASTC4x4 ? bimg::TextureFormat::ASTC4x4
                                                              : bimg::TextureFormat::ETC2A;
            std::vector<uint8_t> decoded(image.rgba.size());
            bimg::imageDecodeToRgba8(&allocator, decoded.data(), v.bytes.data(), 32, 32, 128, f);
            double error = 0;
            for (size_t i = 0; i < decoded.size(); ++i) {
                double d = double(decoded[i]) - image.rgba[i];
                error += d * d;
            }
            double psnr = error == 0 ? 100 : 10 * std::log10(255. * 255. * decoded.size() / error);
            check(psnr > 28, "Compression quality below 28 dB on gradient fixture");
            quality.push_back({{"format", uint32_t(v.format)}, {"psnrDb", psnr}, {"bytes", v.bytes.size()}});
        }
        gm::writeJsonAtomic(root / "compression.json", quality);
        for (auto name : {"builtin:cube", "builtin:plane", "builtin:sphere"})
            bank.meshes.push_back(gm::primitiveMesh(name));
        gm::CookedMaterial mat;
        mat.parameters.push_back({"u_baseColor", {1, .5f, .2f, 1}});
        mat.samplers.push_back({"s_baseColor", 0, 0});
        bank.materials.push_back(mat);
        auto windows = bank;
        gm::targetRenderAssets(windows, "windows");
        check(windows.textures[0].variants.size() == 2, "Windows variant stripping");
        auto android = bank;
        gm::targetRenderAssets(android, "android");
        check(android.textures[0].variants.size() == 3, "Android variant stripping");
        auto binary = gm::encodeRenderAssets(bank);
        check(gm::encodeRenderAssets(gm::decodeRenderAssets(binary)) == binary, "Render bank roundtrip");
        auto broken = binary;
        broken.back() ^= 1;
        rejects([&] { gm::decodeRenderAssets(broken); });
        rejects([&] { gm::decodeRenderAssets(std::span(binary).first(20)); });
        auto bad = bank;
        bad.textures[0].variants[0].bytes.pop_back();
        rejects([&] { gm::validateRenderAssets(bad); });
        bad = bank;
        bad.meshes[0].indices[0] = UINT32_MAX;
        rejects([&] { gm::validateRenderAssets(bad); });
        bad = bank;
        bad.materials[0].parameters[0].name = "u_camera";
        rejects([&] { gm::validateRenderAssets(bad); });
        {
            std::ofstream obj(root / "quad.obj");
            obj << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nf -4 -3 -2 -1\n";
        }
        auto mesh = gm::importObj(root / "quad.obj");
        check(mesh.vertices.size() == 4 && mesh.indices.size() == 6 && mesh.vertices[0].nz > 0,
              "OBJ index/normal generation");
        gm::writeJsonAtomic(root / "surface.material.json",
                            {{"format", "gm.material"},
                             {"version", 1},
                             {"shader", "unlit"},
                             {"textures", {{"s_baseColor", {{"source", "gradient.png"}, {"stage", 0}}}}}});
        auto source = gm::newScene();
        source["entities"] = gm::Json::array(
            {entity("parent", {component("Transform3D", {{"x", 3}, {"rotationY", 90}})}),
             entity(
                 "mesh",
                 {component("Transform3D", {{"z", 2}}),
                  component("MeshRenderer", {{"mesh", "quad.obj"}, {"material", "surface.material.json"}})},
                 "parent"),
             entity("camera", {component("Transform3D", {{"z", -6}}), component("Camera3D")}),
             entity("light", {component("DirectionalLight"), component("PostProcess")})});
        auto compiled = gm::compileScene(source, root);
        auto sceneBytes = gm::encodeScene(compiled);
        gm::SceneRuntime run(gm::decodeScene(sceneBytes));
        check(run.rendering().cameraEnabled && run.rendering().meshes.size() == 1, "3D extraction");
        auto matrix = run.rendering().meshes[0].transform;
        check(std::abs(matrix[12] - 5) < .001f && std::abs(matrix[14]) < .001f, "Hierarchical 3D transform");
        auto e = run.entity(run.find("mesh"));
        gm::sdk::MeshRenderer invalid;
        invalid.mesh = 999;
        auto fields = gm::gameRegistry()->encode(gm::sdk::id("MeshRenderer"), &invalid);
        rejects([&] { run.set(e, gm::sdk::id("MeshRenderer"), fields); });
        auto prefab = gm::newScene();
        prefab["entities"] = gm::Json::array(
            {entity("root", {component("Transform3D"),
                             component("MeshRenderer",
                                       {{"mesh", "builtin:cube"}, {"material", "surface.material.json"}})})});
        auto id = run.loadPrefab(gm::encodeScene(gm::compileScene(prefab, root)));
        run.instantiatePrefab(id);
        check(run.rendering().meshes.size() == 2 && run.asset().rendering.meshes.size() == 2 &&
                  run.asset().rendering.materials[1].samplers[0].texture == 1,
              "Prefab render bank remapping");
        std::set<std::filesystem::path> deps;
        gm::renderDependencies(root, "surface.material.json", deps);
        check(deps.size() == 2, "Material texture dependency");
        auto camera = gm::local3D({1, 2, 3, 20, 40, 10, 2, 3, 4});
        auto identity = gm::multiply3D(camera, gm::inverseAffine3D(camera));
        for (int i = 0; i < 16; ++i)
            check(std::abs(identity[i] - (i % 5 == 0 ? 1.f : 0.f)) < .00001f, "3D inverse");
        auto clip = gm::perspective3D(60, 1, .1f, 100, false);
        check(gm::visibleBounds3D(clip, {-.5f, -.5f, 1}, {.5f, .5f, 2}, false) &&
                  !gm::visibleBounds3D(clip, {100, 0, 1}, {101, 1, 2}, false) &&
                  !gm::visibleBounds3D(clip, {-1, -1, -5}, {1, 1, -4}, false),
              "Frustum classification");
        #include "lighting_asset_tests.inl"
        std::cout << "Render assets: mip gamma/alpha/normals, four codecs, binary validation, OBJ, ECS "
                     "hierarchy, prefab, dependencies passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
