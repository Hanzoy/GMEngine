#include "render_assets.hpp"
#include <bit>
#include <cstring>
#include <set>
namespace gm {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
constexpr uint64_t Budget = 128ull * 1024 * 1024;
void put(std::vector<uint8_t> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(value >> (i * 8)));
}
void floating(std::vector<uint8_t> &out, float value) {
    put(out, std::bit_cast<uint32_t>(value));
}
void blob(std::vector<uint8_t> &out, std::span<const uint8_t> bytes) {
    require(bytes.size() <= Budget, "Render section too large");
    put(out, uint32_t(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}
void name(std::vector<uint8_t> &out, const std::string &value) {
    blob(out, {reinterpret_cast<const uint8_t *>(value.data()), value.size()});
}
uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t h = 2166136261u;
    for (auto b : bytes) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t cursor = 0;
    uint32_t u32() {
        require(cursor + 4 <= bytes.size(), "Truncated render resource");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(bytes[cursor++]) << (8 * i);
        return v;
    }
    float f32() {
        return std::bit_cast<float>(u32());
    }
    uint32_t count(uint32_t limit) {
        auto n = u32();
        require(n <= limit, "Render resource count exceeds limit");
        return n;
    }
    std::vector<uint8_t> blob(uint32_t limit = uint32_t(Budget)) {
        auto n = count(limit);
        require(n <= bytes.size() - cursor, "Truncated render payload");
        std::vector<uint8_t> out(bytes.begin() + cursor, bytes.begin() + cursor + n);
        cursor += n;
        return out;
    }
    std::string name() {
        auto b = blob(64);
        return {b.begin(), b.end()};
    }
};
bool validName(const std::string &name) {
    return name.size() >= 3 && name.size() <= 64 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}
} // namespace
uint64_t textureLevelBytes(TextureFormat format, uint32_t width, uint32_t height) {
    require(format <= TextureFormat::ETC2RGBA, "Unknown texture format");
    return format == TextureFormat::Rgba8 ? uint64_t(width) * height * 4
                                          : uint64_t((width + 3) / 4) * ((height + 3) / 4) * 16;
}
uint64_t textureBytes(const CookedTexture &t, TextureFormat format) {
    uint64_t size = 0;
    auto w = t.width, h = t.height;
    for (uint32_t i = 0; i < t.levels; ++i) {
        size += textureLevelBytes(format, w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return size;
}
void updateMeshBounds(CookedMesh &mesh) {
    require(!mesh.vertices.empty(), "Empty mesh");
    mesh.minimum = mesh.maximum = {mesh.vertices[0].x, mesh.vertices[0].y, mesh.vertices[0].z};
    for (const auto &v : mesh.vertices) {
        float p[] = {v.x, v.y, v.z};
        for (int i = 0; i < 3; ++i) {
            mesh.minimum[i] = std::min(mesh.minimum[i], p[i]);
            mesh.maximum[i] = std::max(mesh.maximum[i], p[i]);
        }
    }
}
void validateRenderAssets(const RenderAssets &a) {
    require(a.textures.size() <= 512 && a.meshes.size() <= 1024 && a.materials.size() <= 4096 &&
                a.shaders.size() <= 128,
            "Too many render resources");
    uint64_t bytes = 0;
    for (const auto &t : a.textures) {
        require(t.width > 0 && t.height > 0 && t.width <= 4096 && t.height <= 4096,
                "Texture dimensions outside 1..4096");
        uint32_t levels = 1;
        for (auto n = std::max(t.width, t.height); n > 1; n /= 2)
            ++levels;
        require(t.levels == 1 || t.levels == levels,
                "Texture must contain level zero or a complete mip chain");
        require(!t.variants.empty() && t.variants.size() <= 4, "Missing/too many texture variants");
        std::set<TextureFormat> formats;
        for (auto &v : t.variants) {
            require(formats.insert(v.format).second && v.bytes.size() == textureBytes(t, v.format),
                    "Invalid/duplicate texture payload");
            bytes += v.bytes.size();
        }
    }
    for (const auto &m : a.meshes) {
        require(!m.vertices.empty() && m.vertices.size() <= 1000000 && !m.indices.empty() &&
                    m.indices.size() <= 3000000 && m.indices.size() % 3 == 0,
                "Invalid mesh sizes");
        for (auto i : m.indices)
            require(i < m.vertices.size(), "Mesh index outside vertex buffer");
        for (auto &v : m.vertices) {
            float f[] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v};
            for (float n : f)
                require(std::isfinite(n) && std::abs(n) <= 1000000, "Invalid mesh vertex");
            require(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz > 1e-10f, "Zero mesh normal");
            for (int i = 0; i < 3; ++i)
                require(f[i] >= m.minimum[i] && f[i] <= m.maximum[i], "Mesh bounds do not contain geometry");
        }
        for (int i = 0; i < 3; ++i)
            require(std::isfinite(m.minimum[i]) && std::isfinite(m.maximum[i]) &&
                        m.minimum[i] <= m.maximum[i],
                    "Invalid mesh bounds");
        bytes += m.vertices.size() * sizeof(MeshVertex) + m.indices.size() * 4;
    }
    for (const auto &s : a.shaders) {
        require(!s.variants.empty() && s.variants.size() <= 2, "Invalid shader variant count");
        std::set<uint32_t> kinds;
        for (auto &v : s.variants) {
            require(v.backend <= 1 && kinds.insert(v.backend).second, "Invalid shader backend");
            require(v.vertex.size() >= 16 && v.fragment.size() >= 16 && v.vertex.size() <= 4 * 1024 * 1024 &&
                        v.fragment.size() <= 4 * 1024 * 1024,
                    "Invalid shader size");
            require(v.vertex[0] == 'V' && v.vertex[1] == 'S' && v.vertex[2] == 'H' && v.fragment[0] == 'F' &&
                        v.fragment[1] == 'S' && v.fragment[2] == 'H',
                    "Shader is not compiled bgfx bytecode");
            bytes += v.vertex.size() + v.fragment.size();
        }
    }
    for (const auto &m : a.materials) {
        require(m.shader == UINT32_MAX || m.shader < a.shaders.size(), "Invalid material shader index");
        require(m.blend <= BlendMode::Additive && m.cull <= CullMode::None && m.parameters.size() <= 32 &&
                    m.samplers.size() <= 8,
                "Invalid material render state");
        std::set<std::string> names;
        std::set<uint32_t> stages;
        for (auto &p : m.parameters) {
            require(validName(p.name) && p.name.starts_with("u_") && names.insert(p.name).second,
                    "Invalid/duplicate material parameter");
            require(p.name != "u_camera" && p.name != "u_lightDirection" && p.name != "u_lightColor",
                    "Engine uniform is reserved");
            for (auto v : p.value)
                require(std::isfinite(v) && std::abs(v) <= 1000000, "Invalid material parameter value");
        }
        for (auto &s : m.samplers)
            require(validName(s.name) && s.name.starts_with("s_") && names.insert(s.name).second &&
                        s.stage < 8 && stages.insert(s.stage).second && s.texture < a.textures.size(),
                    "Invalid material texture binding");
    }
    require(bytes <= Budget, "Render resource bank exceeds 128 MiB");
}
std::vector<uint8_t> encodeRenderAssets(const RenderAssets &a) {
    validateRenderAssets(a);
    std::vector<uint8_t> p;
    put(p, uint32_t(a.textures.size()));
    for (auto &t : a.textures) {
        put(p, t.width);
        put(p, t.height);
        put(p, t.levels);
        put(p, (t.srgb ? 1 : 0) | (t.repeat ? 2 : 0) | (t.linear ? 4 : 0));
        put(p, uint32_t(t.variants.size()));
        for (auto &v : t.variants) {
            put(p, uint32_t(v.format));
            blob(p, v.bytes);
        }
    }
    put(p, uint32_t(a.meshes.size()));
    for (auto &m : a.meshes) {
        for (auto v : m.minimum)
            floating(p, v);
        for (auto v : m.maximum)
            floating(p, v);
        put(p, uint32_t(m.vertices.size()));
        put(p, uint32_t(m.indices.size()));
        for (auto &v : m.vertices)
            for (float n : {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v})
                floating(p, n);
        for (auto i : m.indices)
            put(p, i);
    }
    put(p, uint32_t(a.shaders.size()));
    for (auto &s : a.shaders) {
        put(p, uint32_t(s.variants.size()));
        for (auto &v : s.variants) {
            put(p, v.backend);
            blob(p, v.vertex);
            blob(p, v.fragment);
        }
    }
    put(p, uint32_t(a.materials.size()));
    for (auto &m : a.materials) {
        put(p, m.shader);
        put(p, uint32_t(m.blend));
        put(p, uint32_t(m.cull));
        put(p, (m.depthTest ? 1 : 0) | (m.depthWrite ? 2 : 0));
        put(p, uint32_t(m.parameters.size()));
        for (auto &v : m.parameters) {
            name(p, v.name);
            for (auto n : v.value)
                floating(p, n);
        }
        put(p, uint32_t(m.samplers.size()));
        for (auto &s : m.samplers) {
            name(p, s.name);
            put(p, s.texture);
            put(p, s.stage);
        }
    }
    require(p.size() <= Budget, "Encoded render bank exceeds 128 MiB");
    std::vector<uint8_t> out = {'G', 'M', 'R', 'A'};
    put(out, 1);
    put(out, checksum(p));
    out.insert(out.end(), p.begin(), p.end());
    return out;
}
RenderAssets decodeRenderAssets(std::span<const uint8_t> bytes) {
    require(bytes.size() >= 28 && bytes.size() <= Budget + 12, "Invalid render bank size");
    require(std::memcmp(bytes.data(), "GMRA", 4) == 0, "Expected render bank");
    Reader r{bytes, 4};
    require(r.u32() == 1, "Unsupported render bank version");
    require(r.u32() == checksum(bytes.subspan(12)), "Render bank checksum mismatch");
    RenderAssets a;
    auto count = r.count(512);
    for (uint32_t i = 0; i < count; ++i) {
        CookedTexture t;
        t.width = r.u32();
        t.height = r.u32();
        t.levels = r.u32();
        auto flags = r.u32();
        require(flags <= 7, "Invalid texture flags");
        t.srgb = flags & 1;
        t.repeat = flags & 2;
        t.linear = flags & 4;
        auto n = r.count(4);
        for (uint32_t j = 0; j < n; ++j) {
            auto f = TextureFormat(r.u32());
            t.variants.push_back({f, r.blob()});
        }
        a.textures.push_back(std::move(t));
    }
    count = r.count(1024);
    for (uint32_t i = 0; i < count; ++i) {
        CookedMesh m;
        for (auto &v : m.minimum)
            v = r.f32();
        for (auto &v : m.maximum)
            v = r.f32();
        auto nv = r.count(1000000), ni = r.count(3000000);
        require(uint64_t(nv) * 32 + uint64_t(ni) * 4 <= bytes.size() - r.cursor, "Truncated mesh data");
        m.vertices.resize(nv);
        m.indices.resize(ni);
        for (auto &v : m.vertices) {
            v.x = r.f32();
            v.y = r.f32();
            v.z = r.f32();
            v.nx = r.f32();
            v.ny = r.f32();
            v.nz = r.f32();
            v.u = r.f32();
            v.v = r.f32();
        }
        for (auto &v : m.indices)
            v = r.u32();
        a.meshes.push_back(std::move(m));
    }
    count = r.count(128);
    for (uint32_t i = 0; i < count; ++i) {
        CookedShader s;
        auto n = r.count(2);
        for (uint32_t j = 0; j < n; ++j) {
            auto backend = r.u32();
            auto vs = r.blob(4 * 1024 * 1024), fs = r.blob(4 * 1024 * 1024);
            s.variants.push_back({backend, std::move(vs), std::move(fs)});
        }
        a.shaders.push_back(std::move(s));
    }
    count = r.count(4096);
    for (uint32_t i = 0; i < count; ++i) {
        CookedMaterial m;
        m.shader = r.u32();
        m.blend = BlendMode(r.u32());
        m.cull = CullMode(r.u32());
        auto flags = r.u32();
        require(flags <= 3, "Invalid material flags");
        m.depthTest = flags & 1;
        m.depthWrite = flags & 2;
        auto n = r.count(32);
        for (uint32_t j = 0; j < n; ++j) {
            MaterialParameter p;
            p.name = r.name();
            for (auto &v : p.value)
                v = r.f32();
            m.parameters.push_back(std::move(p));
        }
        n = r.count(8);
        for (uint32_t j = 0; j < n; ++j) {
            auto name = r.name();
            auto texture = r.u32(), stage = r.u32();
            m.samplers.push_back({std::move(name), texture, stage});
        }
        a.materials.push_back(std::move(m));
    }
    require(r.cursor == bytes.size(), "Trailing render resource data");
    validateRenderAssets(a);
    return a;
}
CookedMesh primitiveMesh(const std::string &name) {
    CookedMesh m;
    if (name == "builtin:cube") {
        const Vec3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (auto n : normals) {
            Vec3 up = std::abs(n[1]) > .5f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            auto right = cross3D(up, n);
            up = cross3D(n, right);
            uint32_t base = uint32_t(m.vertices.size());
            for (int i = 0; i < 4; ++i) {
                float u = (i == 1 || i == 2) ? 1.f : 0.f, v = i >= 2 ? 1.f : 0.f;
                Vec3 p{};
                for (int j = 0; j < 3; ++j)
                    p[j] = n[j] * .5f + right[j] * (u - .5f) + up[j] * (v - .5f);
                m.vertices.push_back({p[0], p[1], p[2], n[0], n[1], n[2], u, 1 - v});
            }
            for (uint32_t i : {0, 1, 2, 0, 2, 3})
                m.indices.push_back(base + i);
        }
    } else if (name == "builtin:plane") {
        m.vertices = {{-.5f, 0, -.5f, 0, 1, 0, 0, 0},
                      {-.5f, 0, .5f, 0, 1, 0, 0, 1},
                      {.5f, 0, .5f, 0, 1, 0, 1, 1},
                      {.5f, 0, -.5f, 0, 1, 0, 1, 0}};
        m.indices = {0, 1, 2, 0, 2, 3};
    } else if (name == "builtin:sphere") {
        constexpr int slices = 32, rings = 16;
        constexpr float pi = 3.14159265358979323846f;
        for (int y = 0; y <= rings; ++y)
            for (int x = 0; x <= slices; ++x) {
                float u = float(x) / slices, v = float(y) / rings, sy = std::sin(v * pi);
                Vec3 n{std::cos(u * 2 * pi) * sy, std::cos(v * pi), std::sin(u * 2 * pi) * sy};
                m.vertices.push_back({n[0] * .5f, n[1] * .5f, n[2] * .5f, n[0], n[1], n[2], u, v});
            }
        for (int y = 0; y < rings; ++y)
            for (int x = 0; x < slices; ++x) {
                uint32_t a = y * (slices + 1) + x, b = a + slices + 1;
                for (auto i : {a, b + 1, b, a, a + 1, b + 1})
                    m.indices.push_back(i);
            }
    } else
        throw std::runtime_error("Unknown builtin mesh: " + name);
    updateMeshBounds(m);
    return m;
}
} // namespace gm
