// Members of WorldRenderer::Impl. Lighting uses the same ordered view allocator as the editor.
bgfx::ProgramHandle shadowProgram = BGFX_INVALID_HANDLE, rsmProgram = BGFX_INVALID_HANDLE,
                    skyProgram = BGFX_INVALID_HANDLE;
bgfx::FrameBufferHandle shadowTarget = BGFX_INVALID_HANDLE, rsmTarget = BGFX_INVALID_HANDLE;
bgfx::TextureHandle shadowDepth = BGFX_INVALID_HANDLE;
std::array<bgfx::TextureHandle, 3> rsmTextures{
    {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
uint16_t shadowSize = 0, rsmSize = 0;
uint64_t lightingBytes = 0;
ShadowProjection lightProjection;
bool shadowActive = false, rsmActive = false;
bool lightingCached = false;
std::vector<uint32_t> lightingKey, previousLightingKey;
void clearLighting() {
    releaseHandle(shadowTarget);
    releaseHandle(rsmTarget);
    shadowDepth = BGFX_INVALID_HANDLE;
    rsmTextures.fill(BGFX_INVALID_HANDLE);
    shadowSize = rsmSize = 0;
    lightingBytes = 0;
    lightingCached = false;
}
// Exact key comparison avoids both hash collisions and per-frame scene/texture copies.
// Mesh/texture banks are immutable GPU uploads; bank replacement clears WorldRenderer.
bool lightingUnchanged(const RenderAssets &assets, const RenderFrame &frame) {
    lightingKey.clear();
    lightingKey.reserve(32 + frame.meshes.size() * 18 + assets.materials.size() * 24);
    auto word = [&](uint32_t value) { lightingKey.push_back(value); };
    auto real = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
    for (float v : lightProjection.clip)
        real(v);
    for (float v : lightProjection.origin)
        real(v);
    real(frame.light.intensity);
    word(frame.light.color);
    word(uint32_t(frame.meshes.size()));
    word(uint32_t(assets.materials.size()));
    for (const auto &draw : frame.meshes) {
        word(draw.mesh);
        word(draw.material);
        for (float v : draw.transform)
            real(v);
    }
    for (const auto &material : assets.materials) {
        word(uint32_t(material.blend));
        word(uint32_t(material.cull));
        word(material.depthWrite);
        for (auto name : {"u_baseColor", "u_surface", "u_uvTransform", "u_unlit"}) {
            bool found = false;
            for (const auto &p : material.parameters)
                if (p.name == name) {
                    word(1);
                    for (float v : p.value)
                        real(v);
                    found = true;
                    break;
                }
            if (!found)
                word(0);
        }
        for (auto name : {"s_baseColor", "s_orm"}) {
            uint32_t texture = UINT32_MAX;
            for (const auto &s : material.samplers)
                if (s.name == name) {
                    texture = s.texture;
                    break;
                }
            word(texture);
        }
    }
    return lightingCached && lightingKey == previousLightingKey;
}
void initLighting() {
    shadowProgram = program("vs_mesh", "fs_shadow");
    rsmProgram = program("vs_mesh", "fs_rsm");
    skyProgram = program("vs_post", "fs_sky");
}
void destroyLighting() {
    clearLighting();
    releaseHandle(shadowProgram);
    releaseHandle(rsmProgram);
    releaseHandle(skyProgram);
}
void lightRows() {
    const auto &m = lightProjection.clip;
    vec("u_gmLightRow0", {m[0], m[4], m[8], m[12]});
    vec("u_gmLightRow1", {m[1], m[5], m[9], m[13]});
    vec("u_gmLightRow2", {m[2], m[6], m[10], m[14]});
    const auto *caps = bgfx::getCaps();
    vec("u_gmClipUv", {.5f, caps->originBottomLeft ? .5f : -.5f, caps->homogeneousDepth ? .5f : 1.f,
                       caps->homogeneousDepth ? .5f : 0.f});
    auto o = lightProjection.origin;
    vec("u_gmRsmOrigin", {o[0], o[1], o[2], 0});
}
void environmentUniforms(const RenderAssets &assets, const RenderFrame &frame) {
    const auto &e = frame.environment;
    const bool enabled = e.enabled;
    float angle = e.rotation * .0174532925199433f;
    vec("u_gmEnvironment",
        {enabled ? e.intensity : 0, std::cos(angle), std::sin(angle),
         e.radiance == UINT32_MAX ? 0.f : float(assets.textures.at(e.radiance).levels - 1)});
    // Equirectangular maps wrap only longitude. Clamp latitude to avoid pole contamination.
    const uint32_t flags = BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    bgfx::setTexture(9, uniform("s_gmIrradiance", true),
                     enabled && e.irradiance != UINT32_MAX ? textures.at(e.irradiance) : black, flags);
    bgfx::setTexture(10, uniform("s_gmRadiance", true),
                     enabled && e.radiance != UINT32_MAX ? textures.at(e.radiance) : black, flags);
}
void bindLighting(const RenderAssets &assets, const RenderFrame &frame) {
    lightRows();
    vec("u_gmShadow", {shadowActive ? 1.f : 0.f, shadowSize ? 1.f / shadowSize : 0, frame.shadows.depthBias,
                       frame.shadows.normalBias});
    const auto &clip = lightProjection.clip;
    float depthScale = std::sqrt(clip[2] * clip[2] + clip[6] * clip[6] + clip[10] * clip[10]) *
                       (bgfx::getCaps()->homogeneousDepth ? .5f : 1.f);
    vec("u_gmShadowExtra", {frame.shadows.softness, 2 * lightProjection.extent * depthScale, 0, 0});
    texture(8, "s_gmShadow", shadowActive ? shadowDepth : white);
    environmentUniforms(assets, frame);
    const auto &gi = frame.indirect;
    vec("u_gmRsm", {rsmActive ? 1.f : 0.f, gi.radius / (2 * lightProjection.extent), float(gi.samples),
                    3.14159265f * gi.radius * gi.radius / gi.samples * gi.intensity});
    float minDistance = 2 * lightProjection.extent / std::max(uint16_t(1), rsmSize);
    vec("u_gmRsmLimits", {minDistance * minDistance, gi.radius * gi.radius, 0, 0});
    texture(11, "s_gmRsmPosition", rsmActive ? rsmTextures[0] : black);
    texture(12, "s_gmRsmNormal", rsmActive ? rsmTextures[1] : black);
    texture(13, "s_gmRsmFlux", rsmActive ? rsmTextures[2] : black);
}
void lightingTargets(uint16_t shadowResolution, uint16_t rsmResolution) {
    if (shadowSize == shadowResolution && rsmSize == rsmResolution)
        return;
    clearLighting();
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                           BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    auto depthFormat = bgfx::TextureFormat::D32F;
    if (!bgfx::isTextureValid(0, false, 1, depthFormat, flags))
        depthFormat = bgfx::TextureFormat::D16;
    if (shadowResolution) {
        shadowDepth = bgfx::createTexture2D(shadowResolution, shadowResolution, false, 1, depthFormat, flags);
        if (!bgfx::isValid(shadowDepth))
            throw std::runtime_error("Sampled shadow depth allocation failed");
        shadowTarget = bgfx::createFrameBuffer(1, &shadowDepth, true);
        if (!bgfx::isValid(shadowTarget)) {
            releaseHandle(shadowDepth);
            throw std::runtime_error("Shadow framebuffer allocation failed");
        }
        shadowSize = shadowResolution;
        lightingBytes +=
            uint64_t(shadowSize) * shadowSize * (depthFormat == bgfx::TextureFormat::D16 ? 2 : 4);
    }
    if (rsmResolution) {
        if (bgfx::getCaps()->limits.maxFBAttachments < 3 ||
            !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags))
            throw std::runtime_error("RSM indirect light requires three RGBA16F render targets; disable "
                                     "IndirectLight on this GPU");
        std::array<bgfx::TextureHandle, 4> attachments{
            {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
        for (int i = 0; i < 4; ++i) {
            attachments[i] =
                bgfx::createTexture2D(rsmResolution, rsmResolution, false, 1,
                                      i == 3 ? depthFormat : bgfx::TextureFormat::RGBA16F, flags);
            if (!bgfx::isValid(attachments[i])) {
                for (auto &h : attachments)
                    releaseHandle(h);
                throw std::runtime_error("RSM target allocation failed");
            }
        }
        rsmTarget = bgfx::createFrameBuffer(4, attachments.data(), true);
        if (!bgfx::isValid(rsmTarget)) {
            for (auto &h : attachments)
                releaseHandle(h);
            throw std::runtime_error("RSM framebuffer allocation failed");
        }
        std::copy_n(attachments.begin(), 3, rsmTextures.begin());
        rsmSize = rsmResolution;
        lightingBytes +=
            uint64_t(rsmSize) * rsmSize * (24 + (depthFormat == bgfx::TextureFormat::D16 ? 2 : 4));
    }
}
void lightingPasses(const RenderAssets &assets, const RenderFrame &frame, const ViewCamera3D *camera) {
    shadowActive = (camera || frame.cameraEnabled) && frame.shadows.enabled && frame.light.intensity > 0;
    rsmActive = (camera || frame.cameraEnabled) && frame.indirect.enabled && frame.light.intensity > 0;
    auto limit = bgfx::getCaps()->limits.maxTextureSize;
    lightingTargets(shadowActive ? uint16_t(std::min<uint32_t>(frame.shadows.resolution, limit)) : 0,
                    rsmActive ? uint16_t(std::min<uint32_t>(frame.indirect.resolution, limit)) : 0);
    if (!shadowActive && !rsmActive)
        return;
    lightProjection = shadowProjection(assets, frame, camera ? camera->transform : frame.camera,
                                       std::max(shadowSize, rsmSize), bgfx::getCaps()->homogeneousDepth);
    if (lightingUnchanged(assets, frame)) {
        ++r.frameStats().lightingCacheHits;
        return;
    }
    auto pass = [&](bool gi) {
        auto view = r.nextView();
        auto size = gi ? rsmSize : shadowSize;
        bgfx::setViewName(view, gi ? "GM reflective shadow map" : "GM directional shadows");
        bgfx::setViewFrameBuffer(view, gi ? rsmTarget : shadowTarget);
        bgfx::setViewRect(view, 0, 0, size, size);
        bgfx::setViewTransform(view, lightProjection.view.data(), lightProjection.projection.data());
        if (gi) {
            float clear[4] = {0, 0, 0, 0};
            auto p = r.clearColor(clear);
            bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.f, 0, p, p, p);
        } else
            bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
        bgfx::touch(view);
        const CookedMaterial fallback;
        for (const auto &d : frame.meshes) {
            const auto &m = d.material == UINT32_MAX ? fallback : assets.materials.at(d.material);
            const auto &mesh = assets.meshes.at(d.mesh);
            if (m.blend != BlendMode::Opaque || !m.depthWrite ||
                !visibleBounds3D(multiply3D(lightProjection.clip, d.transform), mesh.minimum, mesh.maximum,
                                 bgfx::getCaps()->homogeneousDepth))
                continue;
            auto parameter = [&](const char *name, std::array<float, 4> value) {
                for (const auto &p : m.parameters)
                    if (p.name == name)
                        return p.value;
                return value;
            };
            vec("u_baseColor", parameter("u_baseColor", {1, 1, 1, 1}));
            vec("u_surface", parameter("u_surface", {0, .6f, 0, 0}));
            vec("u_uvTransform", parameter("u_uvTransform", {1, 1, 0, 0}));
            texture(0, "s_baseColor", white);
            texture(2, "s_orm", white);
            bool orm = false;
            for (const auto &s : m.samplers)
                if (s.name == "s_baseColor")
                    texture(0, "s_baseColor", textures.at(s.texture));
                else if (s.name == "s_orm") {
                    texture(2, "s_orm", textures.at(s.texture));
                    orm = true;
                }
            vec("u_gmRsmMaterial",
                {orm ? 1.f : 0.f, parameter("u_unlit", {0, 0, 0, 0})[0] > .5f ? 0.f : 1.f, 0, 0});
            auto &l = frame.light;
            vec("u_lightDirection", {l.directionX, l.directionY, l.directionZ, 0});
            vec("u_lightColor",
                {float(l.color & 255) / 255 * l.intensity, float((l.color >> 8) & 255) / 255 * l.intensity,
                 float((l.color >> 16) & 255) / 255 * l.intensity, 0});
            lightRows();
            bgfx::setTransform(d.transform.data());
            bgfx::setVertexBuffer(0, meshes.at(d.mesh).vb);
            bgfx::setIndexBuffer(meshes.at(d.mesh).ib);
            uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
            if (gi)
                state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
            if (m.cull == CullMode::Back)
                state |= BGFX_STATE_CULL_CCW;
            else if (m.cull == CullMode::Front)
                state |= BGFX_STATE_CULL_CW;
            bgfx::setState(state);
            bgfx::submit(view, gi ? rsmProgram : shadowProgram);
            auto &s = r.frameStats();
            ++s.drawCalls;
            if (gi)
                ++s.rsmDraws;
            else
                ++s.shadowDraws;
            s.lightingTriangles += uint32_t(mesh.indices.size() / 3);
        }
    };
    if (shadowActive)
        pass(false);
    if (rsmActive)
        pass(true);
    previousLightingKey.swap(lightingKey);
    lightingCached = true;
}
bool sky(const RenderAssets &assets, const RenderFrame &frame, const ViewCamera3D *camera) {
    if (!frame.environment.enabled || !frame.environment.background ||
        frame.environment.radiance == UINT32_MAX || (!camera && !frame.cameraEnabled))
        return false;
    const auto &eye = camera ? camera->transform : frame.camera;
    const auto &lens = camera ? camera->lens : frame.lens;
    float scale = std::tan(lens.fieldOfView * .00872664626f);
    vec("u_gmSkyRight", {eye[0] * scale * width / height, eye[1] * scale * width / height,
                         eye[2] * scale * width / height, 0});
    vec("u_gmSkyUp", {eye[4] * scale, eye[5] * scale, eye[6] * scale, 0});
    vec("u_gmSkyForward", {eye[8], eye[9], eye[10], 0});
    environmentUniforms(assets, frame);
    fullscreen(skyProgram, target, {0, 0, float(width), float(height)}, black, black, false);
    ++r.frameStats().environmentDraws;
    return true;
}
