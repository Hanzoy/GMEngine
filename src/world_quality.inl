// Per-viewport temporal history. No GPU state is stored in the game World.
sdk::RenderQuality quality{0};
bgfx::ProgramHandle aoProgram = BGFX_INVALID_HANDLE, aoBlurProgram = BGFX_INVALID_HANDLE,
                    temporalProgram = BGFX_INVALID_HANDLE;
std::array<bgfx::FrameBufferHandle, 2> aoTargets{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}},
    historyTargets{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
uint64_t qualityBytes = 0;
uint16_t aoWidth = 0, aoHeight = 0;
uint32_t temporalIndex = 0, historyIndex = 0;
uint32_t stationaryFrames = 0, qualityBackground = 0;
bool temporalReuse = false, allowTemporal = true;
bool temporalActive = false, aoActive = false, historyValid = false, depthPrepared = false;
Mat4 qualityEye = identity3D(), qualityProjection = identity3D(), previousVp = identity3D(),
     previousEye = identity3D();
sdk::Camera3D previousLens{};
std::vector<uint32_t> qualityKey, previousQualityKey;
void qualityClear() {
    for (auto &h : aoTargets)
        releaseHandle(h);
    for (auto &h : historyTargets)
        releaseHandle(h);
    qualityBytes = 0;
    historyValid = false;
    temporalIndex = historyIndex = stationaryFrames = 0;
    temporalReuse = false;
}
void initQuality() {
    aoProgram = program("vs_post", "fs_ao");
    aoBlurProgram = program("vs_post", "fs_ao_blur");
    temporalProgram = program("vs_post", "fs_temporal");
}
void destroyQuality() {
    qualityClear();
    releaseHandle(aoProgram);
    releaseHandle(aoBlurProgram);
    releaseHandle(temporalProgram);
}
void qualityTargets() {
    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (aoActive && !bgfx::isValid(aoTargets[0])) {
        aoWidth = std::max(uint16_t(1), uint16_t(width / 2));
        aoHeight = std::max(uint16_t(1), uint16_t(height / 2));
        // RGBA8 is portable on the GLES3 baseline; only the red channel stores visibility.
        for (auto &h : aoTargets) {
            h = bgfx::createFrameBuffer(aoWidth, aoHeight, bgfx::TextureFormat::RGBA8, flags);
            if (!bgfx::isValid(h))
                throw std::runtime_error("AO target allocation failed");
        }
    }
    if (!aoActive)
        for (auto &h : aoTargets)
            releaseHandle(h);
    if (temporalActive && !bgfx::isValid(historyTargets[0])) {
        for (auto &h : historyTargets) {
            h = bgfx::createFrameBuffer(width, height, bgfx::TextureFormat::RGBA16F, flags);
            if (!bgfx::isValid(h))
                throw std::runtime_error("Temporal history allocation failed");
        }
        historyValid = false;
        temporalIndex = historyIndex = 0;
    }
    if (!temporalActive) {
        for (auto &h : historyTargets)
            releaseHandle(h);
        historyValid = false;
        temporalIndex = 0;
    }
    qualityBytes = (aoActive ? uint64_t(aoWidth) * aoHeight * 8 : 0) +
                   (temporalActive ? uint64_t(width) * height * 16 : 0);
}
void qualityBegin(const RenderAssets &assets, const RenderFrame &frame, const ViewCamera3D *camera) {
    quality = frame.quality;
    bool hasCamera = camera || frame.cameraEnabled;
    temporalActive = quality.enabled && quality.temporalAA && hasCamera && hdr && allowTemporal;
    aoActive = quality.enabled && quality.ambientOcclusion && quality.aoIntensity > 0 && hasCamera;
    depthPrepared = false;
    temporalReuse = false;
    qualityTargets();
    qualityEye = camera ? camera->transform : frame.camera;
    const auto &lens = camera ? camera->lens : frame.lens;
    qualityProjection = perspective3D(lens.fieldOfView, float(width) / height, lens.nearPlane, lens.farPlane,
                                      bgfx::getCaps()->homogeneousDepth);
    if (temporalActive) {
        // Conservative history invalidation for dynamic objects/materials until motion vectors exist.
        qualityKey.clear();
        auto append = [&](const auto &value) {
            static_assert(sizeof(value) % 4 == 0);
            const auto *b = reinterpret_cast<const unsigned char *>(&value);
            for (size_t i = 0; i < sizeof(value); i += 4) {
                uint32_t word;
                std::memcpy(&word, b + i, 4);
                qualityKey.push_back(word);
            }
        };
        auto name = [&](const std::string &value) {
            append(uint32_t(value.size()));
            for (unsigned char c : value)
                append(uint32_t(c));
        };
        append(quality);
        append(frame.light);
        append(frame.environment);
        append(frame.indirect);
        append(frame.shadows);
        append(qualityBackground);
        append(uint32_t(frame.meshes.size()));
        append(uint32_t(assets.materials.size()));
        for (const auto &d : frame.meshes) {
            append(d.entity);
            append(d.mesh);
            append(d.material);
            append(d.transform);
        }
        for (const auto &m : assets.materials) {
            append(m.shader);
            append(uint32_t(m.blend));
            append(uint32_t(m.cull));
            append(uint32_t(m.depthTest));
            append(uint32_t(m.depthWrite));
            append(uint32_t(m.parameters.size()));
            append(uint32_t(m.samplers.size()));
            for (const auto &p : m.parameters) {
                name(p.name);
                append(p.value);
            }
            for (const auto &s : m.samplers) {
                name(s.name);
                append(s.stage);
                append(s.texture);
            }
        }
        float distance = 0, forward = 0;
        for (int i = 0; i < 3; ++i) {
            float d = qualityEye[12 + i] - previousEye[12 + i];
            distance += d * d;
            forward += qualityEye[8 + i] * previousEye[8 + i];
        }
        bool cut = distance > 4 || forward < .5f || lens.fieldOfView != previousLens.fieldOfView ||
                   lens.nearPlane != previousLens.nearPlane || lens.farPlane != previousLens.farPlane;
        if (qualityKey != previousQualityKey || cut) {
            historyValid = false;
            temporalIndex = 0;
            stationaryFrames = 0;
        }
        if (historyValid && qualityEye == previousEye)
            ++stationaryFrames;
        else
            stationaryFrames = 0;
        temporalReuse = historyValid && stationaryFrames >= 32;
        previousQualityKey = qualityKey;
        auto halton = [](uint32_t index, uint32_t base) {
            float f = 1, r = 0;
            while (index) {
                f /= base;
                r += f * (index % base);
                index /= base;
            }
            return r;
        };
        uint32_t sample = (temporalReuse ? temporalIndex - 1 : temporalIndex) % 8 + 1;
        qualityProjection[8] = 2 * (halton(sample, 2) - .5f) / width;
        qualityProjection[9] = 2 * (halton(sample, 3) - .5f) / height;
        if (!temporalReuse)
            ++temporalIndex;
    }
    previousLens = lens;
}
void screenUniforms() {
    vec("u_screenProjection",
        {1 / qualityProjection[0], 1 / qualityProjection[5], qualityProjection[10], qualityProjection[14]});
    vec("u_screenSize", {1.f / width, 1.f / height, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f,
                         bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f});
    vec("u_screenJitter", {qualityProjection[8], qualityProjection[9], 0, 0});
    texture(2, "s_depth", depth);
}
void ambientPass(const RenderAssets &assets, const RenderFrame &frame) {
    if (!aoActive)
        return;
    auto view = r.nextView();
    bgfx::setViewName(view, "GM AO depth prepass");
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, width, height);
    bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, uint32_t(0), 1.f, 0);
    bgfx::touch(view);
    auto camera = inverseAffine3D(qualityEye), vp = multiply3D(qualityProjection, camera);
    bgfx::setViewTransform(view, camera.data(), qualityProjection.data());
    const CookedMaterial fallback;
    for (const auto &d : frame.meshes) {
        const auto &m = d.material == UINT32_MAX ? fallback : assets.materials.at(d.material);
        const auto &mesh = assets.meshes.at(d.mesh);
        if (m.blend != BlendMode::Opaque || !m.depthTest || !m.depthWrite ||
            !visibleBounds3D(multiply3D(vp, d.transform), mesh.minimum, mesh.maximum,
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
        for (const auto &s : m.samplers)
            if (s.name == "s_baseColor")
                texture(0, "s_baseColor", textures.at(s.texture));
        bgfx::setTransform(d.transform.data());
        bgfx::setVertexBuffer(0, meshes.at(d.mesh).vb);
        bgfx::setIndexBuffer(meshes.at(d.mesh).ib);
        uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        if (m.cull == CullMode::Back)
            state |= BGFX_STATE_CULL_CCW;
        else if (m.cull == CullMode::Front)
            state |= BGFX_STATE_CULL_CW;
        bgfx::setState(state);
        bgfx::submit(view, shadowProgram);
        ++r.frameStats().drawCalls;
        ++r.frameStats().depthPrepassDraws;
    }
    depthPrepared = true;
    screenUniforms();
    vec("u_ao", {quality.aoRadius, quality.aoIntensity, quality.aoBias, 0});
    Rect rect{0, 0, float(aoWidth), float(aoHeight)};
    fullscreen(aoProgram, aoTargets[0], rect, black, black);
    screenUniforms();
    vec("u_aoBlur", {1.f / aoWidth, 0, 0, 0});
    fullscreen(aoBlurProgram, aoTargets[1], rect, bgfx::getTexture(aoTargets[0]), black);
    screenUniforms();
    vec("u_aoBlur", {0, 1.f / aoHeight, 0, 0});
    fullscreen(aoBlurProgram, aoTargets[0], rect, bgfx::getTexture(aoTargets[1]), black);
    ++r.frameStats().aoPasses;
}
void bindAmbient() {
    auto vp = multiply3D(qualityProjection, inverseAffine3D(qualityEye));
    vec("u_gmScreen0", {vp[0], vp[4], vp[8], vp[12]});
    vec("u_gmScreen1", {vp[1], vp[5], vp[9], vp[13]});
    vec("u_gmScreen3", {vp[3], vp[7], vp[11], vp[15]});
    vec("u_gmAmbient", {aoActive ? 1.f : 0.f, bgfx::getCaps()->originBottomLeft ? .5f : -.5f, 0, 0});
    texture(14, "s_gmAmbient", aoActive ? bgfx::getTexture(aoTargets[0]) : white);
}
bgfx::TextureHandle temporalResolve() {
    if (!temporalActive)
        return color;
    // Exactly unchanged scene inputs: keep the converged image instead of introducing fresh jitter.
    if (temporalReuse) {
        ++r.frameStats().temporalCacheHits;
        return bgfx::getTexture(historyTargets[1 - historyIndex]);
    }
    auto inv = identity3D();
    inv[0] = 1 / qualityProjection[0];
    inv[5] = 1 / qualityProjection[5];
    inv[10] = 0;
    inv[11] = 1 / qualityProjection[14];
    inv[12] = -qualityProjection[8] / qualityProjection[0];
    inv[13] = -qualityProjection[9] / qualityProjection[5];
    inv[14] = 1;
    inv[15] = -qualityProjection[10] / qualityProjection[14];
    auto reprojection = multiply3D(previousVp, multiply3D(qualityEye, inv));
    for (int i = 0; i < 4; ++i)
        vec("u_reproject" + std::to_string(i),
            {reprojection[i], reprojection[i + 4], reprojection[i + 8], reprojection[i + 12]});
    screenUniforms();
    vec("u_temporal", {historyValid ? 1.f : 0.f, quality.historyWeight, 0, 0});
    uint32_t write = historyIndex, read = 1 - write;
    texture(3, "s_history", bgfx::getTexture(historyTargets[read]));
    fullscreen(temporalProgram, historyTargets[write], {0, 0, float(width), float(height)}, color, black);
    auto unjittered = qualityProjection;
    unjittered[8] = unjittered[9] = 0;
    previousVp = multiply3D(unjittered, inverseAffine3D(qualityEye));
    previousEye = qualityEye;
    if (!historyValid)
        ++r.frameStats().temporalResets;
    historyValid = true;
    historyIndex = read;
    ++r.frameStats().temporalPasses;
    return bgfx::getTexture(historyTargets[write]);
}
