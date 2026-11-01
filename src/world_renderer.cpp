#include "world_renderer.hpp"
#include "shadow_math.hpp"
#include <bgfx/embedded_shader.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include "shaders/vs_mesh_dxbc.bin.h"
#include "shaders/vs_mesh_essl.bin.h"
#include "shaders/vs_mesh_instanced_dxbc.bin.h"
#include "shaders/vs_mesh_instanced_essl.bin.h"
#include "shaders/fs_material_dxbc.bin.h"
#include "shaders/fs_material_essl.bin.h"
#include "shaders/vs_post_dxbc.bin.h"
#include "shaders/vs_post_essl.bin.h"
#include "shaders/fs_bloom_dxbc.bin.h"
#include "shaders/fs_bloom_essl.bin.h"
#include "shaders/fs_blur_dxbc.bin.h"
#include "shaders/fs_blur_essl.bin.h"
#include "shaders/fs_post_dxbc.bin.h"
#include "shaders/fs_post_essl.bin.h"
#include "shaders/fs_shadow_dxbc.bin.h"
#include "shaders/fs_shadow_essl.bin.h"
#include "shaders/fs_rsm_dxbc.bin.h"
#include "shaders/fs_rsm_essl.bin.h"
#include "shaders/fs_sky_dxbc.bin.h"
#include "shaders/fs_sky_essl.bin.h"
namespace gm {
namespace {
const bgfx::EmbeddedShader shaders[] = {
    {"vs_mesh",
     {{bgfx::RendererType::Direct3D11, vs_mesh_dxbc, sizeof(vs_mesh_dxbc)},
      {bgfx::RendererType::OpenGLES, vs_mesh_essl, sizeof(vs_mesh_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_mesh_instanced",
     {{bgfx::RendererType::Direct3D11, vs_mesh_instanced_dxbc, sizeof(vs_mesh_instanced_dxbc)},
      {bgfx::RendererType::OpenGLES, vs_mesh_instanced_essl, sizeof(vs_mesh_instanced_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_material",
     {{bgfx::RendererType::Direct3D11, fs_material_dxbc, sizeof(fs_material_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_material_essl, sizeof(fs_material_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"vs_post",
     {{bgfx::RendererType::Direct3D11, vs_post_dxbc, sizeof(vs_post_dxbc)},
      {bgfx::RendererType::OpenGLES, vs_post_essl, sizeof(vs_post_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_bloom",
     {{bgfx::RendererType::Direct3D11, fs_bloom_dxbc, sizeof(fs_bloom_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_bloom_essl, sizeof(fs_bloom_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_blur",
     {{bgfx::RendererType::Direct3D11, fs_blur_dxbc, sizeof(fs_blur_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_blur_essl, sizeof(fs_blur_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_post",
     {{bgfx::RendererType::Direct3D11, fs_post_dxbc, sizeof(fs_post_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_post_essl, sizeof(fs_post_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_shadow",
     {{bgfx::RendererType::Direct3D11, fs_shadow_dxbc, sizeof(fs_shadow_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_shadow_essl, sizeof(fs_shadow_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_rsm",
     {{bgfx::RendererType::Direct3D11, fs_rsm_dxbc, sizeof(fs_rsm_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_rsm_essl, sizeof(fs_rsm_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    {"fs_sky",
     {{bgfx::RendererType::Direct3D11, fs_sky_dxbc, sizeof(fs_sky_dxbc)},
      {bgfx::RendererType::OpenGLES, fs_sky_essl, sizeof(fs_sky_essl)},
      {bgfx::RendererType::Count, nullptr, 0}}},
    BGFX_EMBEDDED_SHADER_END()};
template <class T> void releaseHandle(T &h) {
    if (bgfx::isValid(h))
        bgfx::destroy(h);
    h = BGFX_INVALID_HANDLE;
}
bgfx::ProgramHandle program(const char *vs, const char *fs) {
    auto v = bgfx::createEmbeddedShader(shaders, bgfx::getRendererType(), vs),
         f = bgfx::createEmbeddedShader(shaders, bgfx::getRendererType(), fs);
    if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
        releaseHandle(v);
        releaseHandle(f);
        throw std::runtime_error("Built-in render shader unavailable");
    }
    auto p = bgfx::createProgram(v, f, true);
    if (!bgfx::isValid(p))
        throw std::runtime_error("Cannot create render program");
    return p;
}
bgfx::TextureFormat::Enum format(TextureFormat f) {
    switch (f) {
    case TextureFormat::Rgba16F:
        return bgfx::TextureFormat::RGBA16F;
    case TextureFormat::BC3:
        return bgfx::TextureFormat::BC3;
    case TextureFormat::ASTC4x4:
        return bgfx::TextureFormat::ASTC4x4;
    case TextureFormat::ETC2RGBA:
        return bgfx::TextureFormat::ETC2A;
    default:
        return bgfx::TextureFormat::RGBA8;
    }
}
struct PostVertex {
    float x, y, u, v;
};
} // namespace
struct WorldRenderer::Impl {
    Renderer &r;
    struct Mesh {
        bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
    };
    struct Shader {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        std::map<std::string, bgfx::UniformInfo> uniforms;
    };
    std::vector<Mesh> meshes;
    std::vector<bgfx::TextureHandle> textures;
    std::vector<Shader> custom;
    std::map<std::string, bgfx::UniformHandle, std::less<>> uniforms;
    bgfx::VertexLayout layout, postLayout;
    bgfx::ProgramHandle lit = BGFX_INVALID_HANDLE, instanced = BGFX_INVALID_HANDLE,
                        bloom = BGFX_INVALID_HANDLE, blur = BGFX_INVALID_HANDLE, post = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white = BGFX_INVALID_HANDLE, normal = BGFX_INVALID_HANDLE,
                        black = BGFX_INVALID_HANDLE, color = BGFX_INVALID_HANDLE, depth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE, ping = BGFX_INVALID_HANDLE,
                            pong = BGFX_INVALID_HANDLE;
    uint16_t width = 0, height = 0, bw = 0, bh = 0;
    bool hdr = false;
    uint64_t meshBytes = 0, textureBytes = 0, targetBytes = 0;
    uint32_t compressed = 0, mips = 0;
    Rect viewport{};
    sdk::PostProcess effects{};
    explicit Impl(Renderer &renderer) : r(renderer) {}
#include "world_lighting.inl"
    ~Impl() {
        destroyLighting();
        for (auto &m : meshes) {
            releaseHandle(m.vb);
            releaseHandle(m.ib);
        }
        for (auto &t : textures)
            releaseHandle(t);
        for (auto &s : custom)
            releaseHandle(s.program);
        for (auto &[n, u] : uniforms)
            releaseHandle(u);
        releaseHandle(lit);
        releaseHandle(instanced);
        releaseHandle(bloom);
        releaseHandle(blur);
        releaseHandle(post);
        releaseHandle(white);
        releaseHandle(normal);
        releaseHandle(black);
        targetsClear();
    }
    void targetsClear() {
        releaseHandle(target);
        releaseHandle(ping);
        releaseHandle(pong);
        color = BGFX_INVALID_HANDLE;
        depth = BGFX_INVALID_HANDLE;
        width = height = 0;
        targetBytes = 0;
    }
    bgfx::UniformHandle uniform(std::string_view name, bool sampler = false) {
        auto it = uniforms.find(name);
        if (it != uniforms.end())
            return it->second;
        auto h = bgfx::createUniform(std::string(name).c_str(),
                                     sampler ? bgfx::UniformType::Sampler : bgfx::UniformType::Vec4);
        if (!bgfx::isValid(h))
            throw std::runtime_error("GPU uniform allocation failed: " + std::string(name));
        uniforms.emplace(std::string(name), h);
        return h;
    }
    void vec(const char *name, std::array<float, 4> value) {
        bgfx::setUniform(uniform(name), value.data());
    }
    void texture(uint8_t stage, const char *name, bgfx::TextureHandle h) {
        bgfx::setTexture(stage, uniform(name, true), h);
    }
    void init() {
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        postLayout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        initLighting();
        lit = program("vs_mesh", "fs_material");
        instanced = program("vs_mesh_instanced", "fs_material");
        post = program("vs_post", "fs_post");
        bloom = program("vs_post", "fs_bloom");
        blur = program("vs_post", "fs_blur");
        const uint32_t w = 0xffffffff, n = 0xffff8080, b = 0xff000000;
        white = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&w, 4));
        normal = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&n, 4));
        black = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&b, 4));
        if (!bgfx::isValid(white) || !bgfx::isValid(normal) || !bgfx::isValid(black))
            throw std::runtime_error("Fallback texture allocation failed");
    }
    void upload(const RenderAssets &a) {
        while (meshes.size() < a.meshes.size()) {
            const auto &source = a.meshes[meshes.size()];
            Mesh m;
            m.vb = bgfx::createVertexBuffer(
                bgfx::copy(source.vertices.data(), uint32_t(source.vertices.size() * sizeof(MeshVertex))),
                layout);
            m.ib = bgfx::createIndexBuffer(
                bgfx::copy(source.indices.data(), uint32_t(source.indices.size() * 4)), BGFX_BUFFER_INDEX32);
            if (!bgfx::isValid(m.vb) || !bgfx::isValid(m.ib)) {
                releaseHandle(m.vb);
                releaseHandle(m.ib);
                throw std::runtime_error("Mesh GPU allocation failed");
            }
            meshes.push_back(m);
            uint64_t bytes = source.vertices.size() * sizeof(MeshVertex) + source.indices.size() * 4;
            meshBytes += bytes;
            r.frameStats().vertexUploadBytes += bytes;
        }
        while (textures.size() < a.textures.size()) {
            const auto &t = a.textures[textures.size()];
            uint64_t flags = t.srgb ? BGFX_TEXTURE_SRGB : 0;
            if (!t.repeat)
                flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            if (!t.linear)
                flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
            const TextureVariant *chosen = nullptr;
            std::array<TextureFormat, 5> priority = {TextureFormat::BC3, TextureFormat::ASTC4x4,
                                                     TextureFormat::ETC2RGBA, TextureFormat::Rgba8,
                                                     TextureFormat::Rgba16F};
            if (bgfx::getRendererType() == bgfx::RendererType::OpenGLES)
                priority = {TextureFormat::ASTC4x4, TextureFormat::ETC2RGBA, TextureFormat::BC3,
                            TextureFormat::Rgba8, TextureFormat::Rgba16F};
            for (auto f : priority) {
                for (const auto &v : t.variants)
                    if (v.format == f &&
                        (f != TextureFormat::BC3 || ((t.width % 4) == 0 && (t.height % 4) == 0)) &&
                        (bgfx::getCaps()->formats[format(f)] &
                         (t.srgb ? BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB : BGFX_CAPS_FORMAT_TEXTURE_2D)) &&
                        bgfx::isTextureValid(0, false, 1, format(f), flags)) {
                        chosen = &v;
                        break;
                    }
                if (chosen)
                    break;
            }
            if (!chosen)
                throw std::runtime_error("No supported variant for material texture " +
                                         std::to_string(textures.size()) + " (" + std::to_string(t.width) +
                                         "x" + std::to_string(t.height) +
                                         "); include rgba8 fallback for this GPU/dimension");
            auto h = bgfx::createTexture2D(uint16_t(t.width), uint16_t(t.height), t.levels > 1, 1,
                                           format(chosen->format), flags,
                                           bgfx::copy(chosen->bytes.data(), uint32_t(chosen->bytes.size())));
            if (!bgfx::isValid(h))
                throw std::runtime_error("Material texture GPU allocation failed");
            textures.push_back(h);
            textureBytes += chosen->bytes.size();
            compressed += chosen->format != TextureFormat::Rgba8 && chosen->format != TextureFormat::Rgba16F;
            mips += t.levels;
            r.frameStats().uploadBytes += chosen->bytes.size();
        }
        while (custom.size() < a.shaders.size()) {
            const auto &s = a.shaders[custom.size()];
            uint32_t backend = bgfx::getRendererType() == bgfx::RendererType::OpenGLES ? 1 : 0;
            const ShaderVariant *selected = nullptr;
            for (const auto &v : s.variants)
                if (v.backend == backend)
                    selected = &v;
            if (!selected)
                throw std::runtime_error("Custom shader missing active backend variant");
            auto vs = bgfx::createShader(
                     bgfx::copy(selected->vertex.data(), uint32_t(selected->vertex.size()))),
                 fs = bgfx::createShader(
                     bgfx::copy(selected->fragment.data(), uint32_t(selected->fragment.size())));
            if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
                releaseHandle(vs);
                releaseHandle(fs);
                throw std::runtime_error("Invalid custom shader");
            }
            Shader out;
            for (auto h : {vs, fs}) {
                std::vector<bgfx::UniformHandle> list(bgfx::getShaderUniforms(h));
                bgfx::getShaderUniforms(h, list.data(), uint16_t(list.size()));
                for (auto u : list) {
                    bgfx::UniformInfo info;
                    bgfx::getUniformInfo(u, info);
                    out.uniforms[info.name] = info;
                }
            }
            out.program = bgfx::createProgram(vs, fs, true);
            if (!bgfx::isValid(out.program))
                throw std::runtime_error("Custom program linking failed");
            custom.push_back(std::move(out));
        }
    }
    void targets(uint16_t w, uint16_t h, bool needsBloom) {
        if (w == width && h == height && (!needsBloom || bgfx::isValid(ping)))
            return;
        targetsClear();
        width = w;
        height = h;
        bw = std::max<uint16_t>(1, w / 2);
        bh = std::max<uint16_t>(1, h / 2);
        uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        hdr = bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags);
        auto f = hdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
        auto d = bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)
                     ? bgfx::TextureFormat::D24S8
                     : bgfx::TextureFormat::D16;
        color = bgfx::createTexture2D(w, h, false, 1, f, flags);
        depth = bgfx::createTexture2D(w, h, false, 1, d, BGFX_TEXTURE_RT);
        if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
            releaseHandle(color);
            releaseHandle(depth);
            throw std::runtime_error("Scene render target allocation failed");
        }
        bgfx::TextureHandle attachments[] = {color, depth};
        target = bgfx::createFrameBuffer(2, attachments, true);
        if (!bgfx::isValid(target)) {
            releaseHandle(color);
            releaseHandle(depth);
            throw std::runtime_error("Scene framebuffer allocation failed");
        }
        targetBytes = uint64_t(w) * h * ((hdr ? 8 : 4) + (d == bgfx::TextureFormat::D16 ? 2 : 4));
        if (needsBloom) {
            ping = bgfx::createFrameBuffer(bw, bh, f, flags);
            pong = bgfx::createFrameBuffer(bw, bh, f, flags);
            if (!bgfx::isValid(ping) || !bgfx::isValid(pong))
                throw std::runtime_error("Bloom framebuffer allocation failed");
            targetBytes += uint64_t(bw) * bh * (hdr ? 8 : 4) * 2;
        }
    }
    void bindMaterial(const CookedMaterial &m, const RenderFrame &frame, const Mat4 &eye) {
        if (m.shader == UINT32_MAX) {
            vec("u_baseColor", {1, 1, 1, 1});
            vec("u_surface", {0, .6f, 0, 0});
            vec("u_emission", {0, 0, 0, 0});
            vec("u_unlit", {0, 0, 0, 0});
            vec("u_uvTransform", {1, 1, 0, 0});
            texture(0, "s_baseColor", white);
            texture(1, "s_normal", normal);
        } else {
            const auto &reflection = custom.at(m.shader).uniforms;
            // Reset every reflected value: shared uniforms must not leak between materials.
            for (const auto &[name, info] : reflection) {
                if (info.type == bgfx::UniformType::Sampler && name.starts_with("s_gm") &&
                    name != "s_gmShadow" && name != "s_gmIrradiance" && name != "s_gmRadiance" &&
                    name != "s_gmRsmPosition" && name != "s_gmRsmNormal" && name != "s_gmRsmFlux")
                    throw std::runtime_error("Unknown engine lighting sampler: " + name);
                if (info.num != 1 ||
                    (info.type != bgfx::UniformType::Vec4 && info.type != bgfx::UniformType::Sampler))
                    throw std::runtime_error("Material shader only supports scalar vec4/sampler uniforms: " +
                                             name);
                if (info.type == bgfx::UniformType::Vec4 && !name.starts_with("u_gm") && name != "u_camera" &&
                    name != "u_lightDirection" && name != "u_lightColor")
                    vec(name.c_str(), {0, 0, 0, 0});
            }
            for (const auto &p : m.parameters) {
                auto it = reflection.find(p.name);
                if (it == reflection.end() || it->second.type != bgfx::UniformType::Vec4)
                    throw std::runtime_error("Material parameter missing from shader: " + p.name);
            }
            for (const auto &p : m.samplers) {
                auto it = reflection.find(p.name);
                if (it == reflection.end() || it->second.type != bgfx::UniformType::Sampler)
                    throw std::runtime_error("Material sampler missing from shader: " + p.name);
            }
            for (const auto &[name, info] : reflection)
                if (info.type == bgfx::UniformType::Sampler && !name.starts_with("s_gm") &&
                    std::none_of(m.samplers.begin(), m.samplers.end(),
                                 [&](const auto &p) { return p.name == name; }))
                    throw std::runtime_error("Material must bind shader sampler: " + name);
        }
        for (const auto &p : m.parameters)
            vec(p.name.c_str(), p.value);
        for (const auto &p : m.samplers)
            texture(uint8_t(p.stage), p.name.c_str(), textures.at(p.texture));
        const auto &l = frame.light;
        vec("u_camera", {eye[12], eye[13], eye[14], 1});
        vec("u_lightDirection", {l.directionX, l.directionY, l.directionZ, 0});
        vec("u_lightColor",
            {float(l.color & 255) / 255 * l.intensity, float((l.color >> 8) & 255) / 255 * l.intensity,
             float((l.color >> 16) & 255) / 255 * l.intensity, l.ambient});
    }
    void draw(const RenderAssets &assets, const RenderFrame &frame, uint16_t view, bool useInstances,
              const ViewCamera3D *overrideCamera) {
        if (!frame.cameraEnabled && !overrideCamera)
            return;
        const auto &eye = overrideCamera ? overrideCamera->transform : frame.camera;
        const auto &lens = overrideCamera ? overrideCamera->lens : frame.lens;
        auto camera = inverseAffine3D(eye),
             projection = perspective3D(lens.fieldOfView, float(width) / height, lens.nearPlane,
                                        lens.farPlane, bgfx::getCaps()->homogeneousDepth);
        auto vp = multiply3D(projection, camera);
        bgfx::setViewTransform(view, camera.data(), projection.data());
        struct Item {
            const MeshDraw *draw;
            float distance;
        };
        std::vector<Item> opaque, transparent;
        auto &stats = r.frameStats();
        stats.totalMeshes += uint32_t(frame.meshes.size());
        const CookedMaterial fallback;
        auto material = [&](const MeshDraw &d) -> const CookedMaterial & {
            return d.material == UINT32_MAX ? fallback : assets.materials.at(d.material);
        };
        for (const auto &d : frame.meshes) {
            const auto &mesh = assets.meshes.at(d.mesh);
            if (!visibleBounds3D(multiply3D(vp, d.transform), mesh.minimum, mesh.maximum,
                                 bgfx::getCaps()->homogeneousDepth))
                continue;
            auto center = transformPoint(d.transform, {(mesh.minimum[0] + mesh.maximum[0]) * .5f,
                                                       (mesh.minimum[1] + mesh.maximum[1]) * .5f,
                                                       (mesh.minimum[2] + mesh.maximum[2]) * .5f});
            float distance = transformPoint(camera, center)[2];
            (material(d).blend == BlendMode::Opaque ? opaque : transparent).push_back({&d, distance});
            ++stats.visibleMeshes;
        }
        std::stable_sort(opaque.begin(), opaque.end(), [](auto a, auto b) {
            return std::pair{a.draw->material, a.draw->mesh} < std::pair{b.draw->material, b.draw->mesh};
        });
        std::stable_sort(transparent.begin(), transparent.end(),
                         [](auto a, auto b) { return a.distance > b.distance; });
        auto submit = [&](const std::vector<Item> &list, bool batch) {
            for (size_t first = 0; first < list.size();) {
                const auto &d = *list[first].draw;
                const auto &m = material(d);
                size_t end = first + 1;
                if (batch && useInstances && m.shader == UINT32_MAX)
                    while (end < list.size() && list[end].draw->mesh == d.mesh &&
                           list[end].draw->material == d.material)
                        ++end;
                uint32_t count = uint32_t(end - first);
                bool instance = count > 1 && bgfx::getAvailInstanceDataBuffer(count, 64) == count;
                if (!instance) {
                    count = 1;
                    end = first + 1;
                }
                bindMaterial(m, frame, eye);
                bindLighting(assets, frame);
                bgfx::setVertexBuffer(0, meshes.at(d.mesh).vb);
                bgfx::setIndexBuffer(meshes.at(d.mesh).ib);
                if (instance) {
                    bgfx::InstanceDataBuffer buffer;
                    bgfx::allocInstanceDataBuffer(&buffer, count, 64);
                    for (uint32_t i = 0; i < count; ++i)
                        std::memcpy(buffer.data + i * 64, list[first + i].draw->transform.data(), 64);
                    bgfx::setInstanceDataBuffer(&buffer);
                    stats.vertexUploadBytes += count * 64;
                    stats.instances += count;
                } else
                    bgfx::setTransform(d.transform.data());
                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                if (m.depthTest)
                    state |= BGFX_STATE_DEPTH_TEST_LESS;
                if (m.depthWrite)
                    state |= BGFX_STATE_WRITE_Z;
                if (m.cull == CullMode::Back)
                    state |= BGFX_STATE_CULL_CCW;
                else if (m.cull == CullMode::Front)
                    state |= BGFX_STATE_CULL_CW;
                if (m.blend == BlendMode::Alpha)
                    state |= BGFX_STATE_BLEND_ALPHA;
                else if (m.blend == BlendMode::Additive)
                    state |= BGFX_STATE_BLEND_ADD;
                bgfx::setState(state);
                bgfx::submit(view, m.shader == UINT32_MAX ? (instance ? instanced : lit)
                                                          : custom.at(m.shader).program);
                ++stats.drawCalls;
                ++stats.meshDraws;
                stats.triangles += uint32_t(assets.meshes[d.mesh].indices.size() / 3) * count;
                first = end;
            }
        };
        submit(opaque, true);
        submit(transparent, false);
    }
    void fullscreen(bgfx::ProgramHandle p, bgfx::FrameBufferHandle fb, Rect rect, bgfx::TextureHandle input,
                    bgfx::TextureHandle bloomInput, bool postPass = true) {
        auto view = r.nextView();
        bgfx::setViewFrameBuffer(view, fb);
        bgfx::setViewRect(view, uint16_t(std::max(0.f, rect.x)), uint16_t(std::max(0.f, rect.y)),
                          uint16_t(rect.w), uint16_t(rect.h));
        bgfx::setViewTransform(view, nullptr, nullptr);
        bgfx::setViewName(view, postPass ? "GM post process" : "GM environment sky");
        bgfx::setViewClear(view, BGFX_CLEAR_NONE);
        if (bgfx::getAvailTransientVertexBuffer(4, postLayout) < 4 ||
            bgfx::getAvailTransientIndexBuffer(6) < 6)
            throw std::runtime_error("Postprocess transient buffer capacity exceeded");
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        bgfx::allocTransientVertexBuffer(&vb, 4, postLayout);
        bgfx::allocTransientIndexBuffer(&ib, 6);
        const PostVertex verts[] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}};
        const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
        std::memcpy(vb.data, verts, sizeof(verts));
        std::memcpy(ib.data, indices, sizeof(indices));
        bgfx::setVertexBuffer(0, &vb);
        bgfx::setIndexBuffer(&ib);
        vec("u_postUv", {postPass && bgfx::getCaps()->originBottomLeft ? 1.f : 0.f, 0, 0, 0});
        texture(0, "s_scene", input);
        texture(1, "s_bloom", bloomInput);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, p);
        ++r.frameStats().drawCalls;
        if (postPass)
            ++r.frameStats().postPasses;
        r.frameStats().vertexUploadBytes += sizeof(verts) + sizeof(indices);
    }
    void finish() {
        // Flush 2D world commands before submitting any post views.
        r.nextView();
        vec("u_bloom", {effects.bloomThreshold, effects.bloom ? effects.bloomIntensity : 0, 0, 0});
        if (effects.bloom) {
            fullscreen(bloom, ping, {0, 0, float(bw), float(bh)}, color, black);
            vec("u_blur", {effects.bloomRadius / bw, 0, 0, 0});
            fullscreen(blur, pong, {0, 0, float(bw), float(bh)}, bgfx::getTexture(ping), black);
            vec("u_blur", {0, effects.bloomRadius / bh, 0, 0});
            fullscreen(blur, ping, {0, 0, float(bw), float(bh)}, bgfx::getTexture(pong), black);
        }
        vec("u_post", {effects.exposure, effects.gamma, float(effects.toneMapping), float(effects.fxaa)});
        vec("u_postSize", {1.f / width, 1.f / height, 0, 0});
        vec("u_bloom", {effects.bloomThreshold, effects.bloom ? effects.bloomIntensity : 0, 0, 0});
        fullscreen(post, BGFX_INVALID_HANDLE, viewport, color,
                   effects.bloom ? bgfx::getTexture(ping) : black);
        r.backbuffer();
    }
};
WorldRenderer::WorldRenderer(Renderer &renderer) : renderer_(renderer) {}
WorldRenderer::~WorldRenderer() = default;
void WorldRenderer::clear() {
    impl_.reset();
}
void WorldRenderer::instancing(bool enabled) {
    instancing_ = enabled;
}
bool WorldRenderer::begin(const RenderAssets &assets, const RenderFrame &frame, Rect viewport,
                          uint32_t background, const ViewCamera3D *camera) {
    if (!camera && !frame.cameraEnabled && !frame.post.enabled)
        return false;
    if (viewport.w < 1 || viewport.h < 1)
        return false;
    if (!impl_) {
        auto next = std::make_unique<Impl>(renderer_);
        next->init();
        impl_ = std::move(next);
    }
    auto &g = *impl_;
    g.upload(assets);
    g.viewport = viewport;
    g.effects = frame.post;
    if (!g.effects.enabled) {
        g.effects = {};
        g.effects.toneMapping = 0;
        g.effects.fxaa = 0;
        g.effects.bloom = 0;
    }
    auto limit = std::min<uint32_t>(4096, bgfx::getCaps()->limits.maxTextureSize);
    const float targetScale =
        std::min({g.effects.renderScale, float(limit) / viewport.w, float(limit) / viewport.h});
    g.targets(uint16_t(std::max(1.f, viewport.w * targetScale)),
              uint16_t(std::max(1.f, viewport.h * targetScale)), g.effects.bloom != 0);
    g.lightingPasses(assets, frame, camera);
    bool hasSky = g.sky(assets, frame, camera);
    auto view = renderer_.nextView();
    bgfx::setViewName(view, "GM opaque and transparent 3D");
    bgfx::setViewFrameBuffer(view, g.target);
    bgfx::setViewRect(view, 0, 0, g.width, g.height);
    auto linear = [](uint32_t b) {
        float f = float(b) / 255;
        return f <= .04045f ? f / 12.92f : std::pow((f + .055f) / 1.055f, 2.4f);
    };
    float clear[4] = {linear(background & 255), linear((background >> 8) & 255),
                      linear((background >> 16) & 255), 1};
    auto palette = renderer_.clearColor(clear);
    if (hasSky)
        bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, uint32_t(0), 1.f, 0);
    else
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.f, 0, palette);
    bgfx::touch(view);
    g.draw(assets, frame, view, instancing_, camera);
    renderer_.layer2D(g.target, viewport, g.width, g.height, true);
    auto &s = renderer_.frameStats();
    s.meshBytes += g.meshBytes;
    s.materialTextureBytes += g.textureBytes;
    s.targetBytes += g.targetBytes + g.lightingBytes;
    s.lightingBytes += g.lightingBytes;
    s.compressedTextures += g.compressed;
    s.mipLevels += g.mips;
    s.hdr = g.hdr;
    return true;
}
void WorldRenderer::finish() {
    if (impl_)
        impl_->finish();
}
} // namespace gm
