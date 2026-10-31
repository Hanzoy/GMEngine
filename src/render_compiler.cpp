#include "render_compiler.hpp"
#include "image_import.hpp"
#include "process.hpp"
#include <fstream>
#include <sstream>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <charconv>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace gm {
namespace {
std::string text(const std::filesystem::path &p) {
    auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
std::vector<uint8_t> readBytes(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    auto n = f.tellg();
    if (!f || n < 0 || n > 4 * 1024 * 1024)
        throw std::runtime_error("Invalid compiled shader: " + text(path));
    std::vector<uint8_t> bytes(size_t(n), 0);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(bytes.data()), n);
    if (!f)
        throw std::runtime_error("Cannot read shader: " + text(path));
    return bytes;
}
std::filesystem::path shaderCompiler() {
#ifdef _WIN32
    wchar_t configured[32768];
    auto configuredLength = GetEnvironmentVariableW(L"GM_SHADERC", configured, 32768);
    if (configuredLength) {
        if (configuredLength >= 32768)
            throw std::runtime_error("GM_SHADERC path is too long");
        std::filesystem::path p(configured);
        if (!std::filesystem::is_regular_file(p))
            throw std::runtime_error("GM_SHADERC does not name shaderc.exe");
        return p;
    }
    wchar_t executable[32768];
    auto n = GetModuleFileNameW(nullptr, executable, 32768);
    if (n && n < 32768) {
        auto p = std::filesystem::path(executable).parent_path().parent_path() / "tools/shaderc.exe";
        if (std::filesystem::exists(p))
            return p;
    }
#else
    if (const char *configured = std::getenv("GM_SHADERC"))
        return configured;
#endif
    auto local = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                 "GMEngineBuild/shader-tools/cmake/bgfx/Release/shaderc.exe";
    if (std::filesystem::exists(local))
        return local;
    throw std::runtime_error(
        "Custom shader source needs shaderc. Set GM_SHADERC or supply precompiled variants in .shader.json");
}
} // namespace
RenderCompiler::RenderCompiler(RenderAssets &assets) : assets_(assets) {
    validateRenderAssets(assets);
    for (const auto &t : assets.textures)
        for (const auto &v : t.variants)
            addBytes(v.bytes.size());
    for (const auto &m : assets.meshes)
        addBytes(m.vertices.size() * sizeof(MeshVertex) + m.indices.size() * 4);
    for (const auto &shader : assets.shaders)
        for (const auto &v : shader.variants)
            addBytes(v.vertex.size() + v.fragment.size());
}
void RenderCompiler::addBytes(uint64_t bytes) {
    if (bytes > 128ull * 1024 * 1024 - payloadBytes_)
        throw std::runtime_error("Render resources exceed 128 MiB");
    payloadBytes_ += bytes;
}
CookedMesh importObj(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 64 * 1024 * 1024)
        throw std::runtime_error("OBJ source exceeds 64 MiB");
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read OBJ");
    std::vector<Vec3> positions, normals;
    std::vector<std::array<float, 2>> uvs;
    CookedMesh mesh;
    std::map<std::array<uint32_t, 8>, uint32_t> vertices;
    std::string line;
    auto integer = [](const std::string &value) {
        int out = 0;
        auto result = std::from_chars(value.data(), value.data() + value.size(), out);
        if (result.ec != std::errc() || result.ptr != value.data() + value.size())
            throw std::runtime_error("Invalid OBJ index");
        return out;
    };
    auto index = [](int value, size_t count) {
        auto i = value > 0 ? int64_t(value) - 1 : int64_t(count) + value;
        if (value == 0 || i < 0 || i >= int64_t(count))
            throw std::runtime_error("OBJ index out of range");
        return size_t(i);
    };
    while (std::getline(file, line)) {
        if (line.size() > 1024 * 1024)
            throw std::runtime_error("OBJ line too long");
        std::istringstream in(line);
        std::string type;
        in >> type;
        if (type == "v" || type == "vn") {
            Vec3 p;
            if (!(in >> p[0] >> p[1] >> p[2]))
                throw std::runtime_error("Invalid OBJ position/normal");
            (type == "v" ? positions : normals).push_back(p);
        } else if (type == "vt") {
            std::array<float, 2> uv;
            if (!(in >> uv[0] >> uv[1]))
                throw std::runtime_error("Invalid OBJ UV");
            uvs.push_back(uv);
        } else if (type == "f") {
            struct Corner {
                Vec3 p, n;
                std::array<float, 2> uv;
                bool hasNormal = false;
            };
            std::vector<Corner> face;
            std::string token;
            while (in >> token) {
                if (token.starts_with('#'))
                    break;
                std::string parts[3];
                size_t from = 0;
                for (int i = 0; i < 3; ++i) {
                    auto end = token.find('/', from);
                    parts[i] = token.substr(from, end == std::string::npos ? end : end - from);
                    if (end == std::string::npos)
                        break;
                    from = end + 1;
                }
                Corner c{};
                c.p = positions.at(index(integer(parts[0]), positions.size()));
                if (!parts[1].empty()) {
                    c.uv = uvs.at(index(integer(parts[1]), uvs.size()));
                    c.uv[1] = 1 - c.uv[1];
                }
                if (!parts[2].empty()) {
                    c.n = normalize3D(normals.at(index(integer(parts[2]), normals.size())));
                    c.hasNormal = true;
                }
                face.push_back(c);
                if (face.size() > 256)
                    throw std::runtime_error("OBJ face exceeds 256 corners");
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face needs three corners");
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                Corner triangle[] = {face[0], face[i], face[i + 1]};
                Vec3 a{}, b{};
                for (int j = 0; j < 3; ++j) {
                    a[j] = triangle[1].p[j] - triangle[0].p[j];
                    b[j] = triangle[2].p[j] - triangle[0].p[j];
                }
                auto n = normalize3D(cross3D(a, b));
                for (auto &c : triangle) {
                    if (!c.hasNormal)
                        c.n = n;
                    MeshVertex v{c.p[0], c.p[1], c.p[2], c.n[0], c.n[1], c.n[2], c.uv[0], c.uv[1]};
                    auto key = std::bit_cast<std::array<uint32_t, 8>>(v);
                    auto [it, created] = vertices.emplace(key, uint32_t(mesh.vertices.size()));
                    if (created)
                        mesh.vertices.push_back(v);
                    mesh.indices.push_back(it->second);
                }
            }
        }
        if (positions.size() > 1000000 || normals.size() > 1000000 || uvs.size() > 1000000 ||
            mesh.vertices.size() > 1000000 || mesh.indices.size() > 3000000)
            throw std::runtime_error("OBJ geometry budget exceeded");
    }
    updateMeshBounds(mesh);
    RenderAssets test;
    test.meshes.push_back(mesh);
    validateRenderAssets(test);
    return mesh;
}
std::filesystem::path RenderCompiler::resolve(const std::filesystem::path &directory,
                                              const std::string &reference) {
    auto p = std::filesystem::canonical(resolveResource(directory, reference));
    dependencies_.insert(p);
    return p;
}
uint32_t RenderCompiler::mesh(const std::filesystem::path &directory, const std::string &reference) {
    if (reference.empty())
        return UINT32_MAX;
    auto key = reference.starts_with("builtin:") ? reference : text(resolve(directory, reference));
    if (auto it = meshes_.find(key); it != meshes_.end())
        return it->second;
    if (assets_.meshes.size() >= 1024)
        throw std::runtime_error("Too many meshes");
    CookedMesh value;
    if (reference.starts_with("builtin:"))
        value = primitiveMesh(reference);
    else {
        auto path = resolve(directory, reference);
        if (path.extension() != ".obj")
            throw std::runtime_error("Mesh import currently accepts .obj");
        value = importObj(path);
    }
    auto id = uint32_t(assets_.meshes.size());
    addBytes(value.vertices.size() * sizeof(MeshVertex) + value.indices.size() * 4);
    assets_.meshes.push_back(std::move(value));
    meshes_[key] = id;
    return id;
}
uint32_t RenderCompiler::texture(const std::filesystem::path &directory, const std::string &reference,
                                 bool normal) {
    auto path = resolve(directory, reference);
    auto key = text(path) + (normal ? ":normal" : ":color");
    if (auto it = textures_.find(key); it != textures_.end())
        return it->second;
    if (assets_.textures.size() >= 512)
        throw std::runtime_error("Too many textures");
    TextureSettings settings;
    settings.normalMap = normal;
    settings.srgb = !normal;
    if (path.extension() == ".json") {
        auto source = readJson(path);
        if (source.value("format", std::string{}) != "gm.texture" || source.value("version", 0) != 1)
            throw std::runtime_error("Expected gm.texture version 1");
        settings.normalMap = source.value("normalMap", normal);
        settings.srgb = source.value("srgb", !settings.normalMap);
        settings.mipmaps = source.value("mipmaps", true);
        settings.repeat = source.value("repeat", true);
        settings.linear = source.value("linear", true);
        if (source.contains("formats")) {
            settings.formats.clear();
            for (auto &f : source.at("formats")) {
                if (f == "rgba8")
                    settings.formats.push_back(TextureFormat::Rgba8);
                else if (f == "bc3")
                    settings.formats.push_back(TextureFormat::BC3);
                else if (f == "astc4x4")
                    settings.formats.push_back(TextureFormat::ASTC4x4);
                else if (f == "etc2rgba")
                    settings.formats.push_back(TextureFormat::ETC2RGBA);
                else
                    throw std::runtime_error("Unknown texture format: " + f.dump());
            }
        }
        path = resolve(path.parent_path(), source.at("source").get<std::string>());
    }
    auto value = cookTexture(importImage(path), settings);
    auto id = uint32_t(assets_.textures.size());
    for (const auto &v : value.variants)
        addBytes(v.bytes.size());
    assets_.textures.push_back(std::move(value));
    textures_[key] = id;
    return id;
}
uint32_t RenderCompiler::shader(const std::filesystem::path &directory, const std::string &reference) {
    auto path = resolve(directory, reference);
    auto key = text(path);
    if (auto it = shaders_.find(key); it != shaders_.end())
        return it->second;
    auto source = readJson(path);
    if (source.value("format", std::string{}) != "gm.shader" || source.value("version", 0) != 1)
        throw std::runtime_error("Expected gm.shader version 1");
    if (assets_.shaders.size() >= 128)
        throw std::runtime_error("Too many shaders");
    CookedShader shader;
    if (source.contains("variants")) {
        for (auto &variant : source.at("variants")) {
            auto backend = variant.at("backend").get<std::string>();
            if (backend != "d3d11" && backend != "gles3")
                throw std::runtime_error("Unsupported shader backend");
            shader.variants.push_back(
                {backend == "d3d11" ? 0u : 1u,
                 readBytes(resolve(path.parent_path(), variant.at("vertex").get<std::string>())),
                 readBytes(resolve(path.parent_path(), variant.at("fragment").get<std::string>()))});
        }
    } else {
        auto compiler = shaderCompiler();
        auto vertex = resolve(path.parent_path(), source.at("vertex").get<std::string>()),
             fragment = resolve(path.parent_path(), source.at("fragment").get<std::string>()),
             varying = resolve(path.parent_path(), source.at("varying").get<std::string>());
        auto temporary =
            std::filesystem::temp_directory_path() /
            ("gm-shader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{temporary};
        for (uint32_t backend = 0; backend < 2; ++backend) {
            ShaderVariant variant;
            variant.backend = backend;
            for (int stage = 0; stage < 2; ++stage) {
                auto input = stage ? fragment : vertex,
                     output = temporary / (stage ? "fragment.bin" : "vertex.bin");
                std::vector<std::wstring> args{compiler.wstring(),
                                               L"-f",
                                               input.wstring(),
                                               L"-o",
                                               output.wstring(),
                                               L"--type",
                                               stage ? L"fragment" : L"vertex",
                                               L"--platform",
                                               backend ? L"android" : L"windows",
                                               L"-p",
                                               backend ? L"300_es" : L"s_5_0",
                                               L"--varyingdef",
                                               varying.wstring(),
                                               L"-i",
                                               path.parent_path().wstring(),
                                               L"-O",
                                               L"3"};
                auto standard = compiler.parent_path().parent_path() / "sdk/shaders";
                if (!std::filesystem::exists(standard))
                    standard =
                        std::filesystem::path(__FILE__).parent_path().parent_path() / "third_party/bgfx/src";
                if (std::filesystem::exists(standard)) {
                    args.push_back(L"-i");
                    args.push_back(standard.wstring());
                }
                for (auto &include : source.value("includeDirectories", Json::array())) {
                    args.push_back(L"-i");
                    args.push_back(resolve(path.parent_path(), include.get<std::string>()).wstring());
                }
                auto log = temporary / "compile.log";
                if (runProcess(args, path.parent_path(), log) != 0) {
                    std::ifstream in(log);
                    std::string message((std::istreambuf_iterator<char>(in)), {});
                    throw std::runtime_error("Shader compilation failed: " + text(path) + "\n" +
                                             message.substr(0, 8192));
                }
                (stage ? variant.fragment : variant.vertex) = readBytes(output);
            }
            shader.variants.push_back(std::move(variant));
        }
    }
    RenderAssets test;
    test.shaders.push_back(shader);
    validateRenderAssets(test);
    auto id = uint32_t(assets_.shaders.size());
    for (const auto &v : shader.variants)
        addBytes(v.vertex.size() + v.fragment.size());
    assets_.shaders.push_back(std::move(shader));
    shaders_[key] = id;
    return id;
}
CookedMaterial RenderCompiler::materialSource(const Json &source, const std::filesystem::path &directory) {
    auto s = normalizedMaterial(source);
    CookedMaterial m;
    auto name = s["shader"].get<std::string>();
    if (name != "lit" && name != "unlit")
        m.shader = shader(directory, name);
    m.blend = s["blend"] == "opaque"  ? BlendMode::Opaque
              : s["blend"] == "alpha" ? BlendMode::Alpha
                                      : BlendMode::Additive;
    m.cull = s["cull"] == "back" ? CullMode::Back : s["cull"] == "front" ? CullMode::Front : CullMode::None;
    m.depthTest = s["depthTest"].get<bool>();
    m.depthWrite = s["depthWrite"].get<bool>();
    for (auto &[key, value] : s["parameters"].items())
        m.parameters.push_back({key, value.get<std::array<float, 4>>()});
    if (name == "unlit" && !s["parameters"].contains("u_unlit"))
        m.parameters.push_back({"u_unlit", {1, 0, 0, 0}});
    for (auto &[key, value] : s["textures"].items())
        m.samplers.push_back({key, texture(directory, value["source"].get<std::string>(), key == "s_normal"),
                              value["stage"].get<uint32_t>()});
    return m;
}
uint32_t RenderCompiler::material(const std::filesystem::path &directory, const std::string &reference) {
    if (reference.empty())
        return UINT32_MAX;
    auto path = resolve(directory, reference);
    auto key = text(path);
    if (auto it = materials_.find(key); it != materials_.end())
        return it->second;
    if (assets_.materials.size() >= 4096)
        throw std::runtime_error("Too many materials");
    auto value = materialSource(readJson(path), path.parent_path());
    auto id = uint32_t(assets_.materials.size());
    assets_.materials.push_back(std::move(value));
    materials_[key] = id;
    return id;
}
void renderDependencies(const std::filesystem::path &directory, const std::string &reference,
                        std::set<std::filesystem::path> &output, uint32_t depth) {
    if (depth > 64 || output.size() > 4096)
        throw std::runtime_error("Too many rendering dependencies");
    if (reference.empty() || reference.starts_with("builtin:"))
        return;
    auto path = std::filesystem::canonical(resolveResource(directory, reference));
    if (!output.insert(path).second)
        return;
    if (path.extension() != ".json")
        return;
    auto s = readJson(path);
    auto recurse = [&](const std::string &r) {
        renderDependencies(path.parent_path(), r, output, depth + 1);
    };
    auto format = s.value("format", std::string{});
    if (format == "gm.material") {
        s = normalizedMaterial(s);
        auto shader = s["shader"].get<std::string>();
        if (shader != "lit" && shader != "unlit")
            recurse(shader);
        for (auto &v : s["textures"].items())
            recurse(v.value()["source"].get<std::string>());
    } else if (format == "gm.texture")
        recurse(s.at("source").get<std::string>());
    else if (format == "gm.shader") {
        if (s.contains("variants"))
            for (auto &v : s["variants"]) {
                recurse(v.at("vertex").get<std::string>());
                recurse(v.at("fragment").get<std::string>());
            }
        else {
            for (auto key : {"vertex", "fragment", "varying"})
                recurse(s.at(key).get<std::string>());
            for (auto &dir : s.value("includeDirectories", Json::array())) {
                auto base =
                    std::filesystem::canonical(resolveResource(path.parent_path(), dir.get<std::string>()));
                for (auto &item : std::filesystem::recursive_directory_iterator(base))
                    if (item.is_regular_file())
                        output.insert(std::filesystem::canonical(item.path()));
            }
            for (auto &item : std::filesystem::directory_iterator(path.parent_path()))
                if (item.is_regular_file() &&
                    (item.path().extension() == ".sc" || item.path().extension() == ".sh"))
                    output.insert(std::filesystem::canonical(item.path()));
        }
    }
}
void targetRenderAssets(RenderAssets &assets, const std::string &platform) {
    if (platform != "windows" && platform != "android")
        throw std::runtime_error("Texture target must be windows or android");
    auto candidate = assets;
    for (auto &t : candidate.textures) {
        std::erase_if(t.variants, [&](const TextureVariant &v) {
            return v.format != TextureFormat::Rgba8 &&
                   (platform == "windows" ? v.format != TextureFormat::BC3 : v.format == TextureFormat::BC3);
        });
        if (t.variants.empty())
            throw std::runtime_error("Texture has no variant for target platform");
    }
    for (auto &s : candidate.shaders) {
        std::erase_if(s.variants,
                      [&](const ShaderVariant &v) { return v.backend != (platform == "windows" ? 0u : 1u); });
        if (s.variants.empty())
            throw std::runtime_error("Shader has no target platform variant");
    }
    validateRenderAssets(candidate);
    assets = std::move(candidate);
}

} // namespace gm
