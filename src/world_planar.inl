bgfx::FrameBufferHandle planarTarget = BGFX_INVALID_HANDLE;
uint16_t planarWidth = 0, planarHeight = 0;
uint64_t planarBytes = 0;
bool reflecting = false;
Mat4 planarVp = identity3D();
void planarClear() {
    releaseHandle(planarTarget);
    planarWidth = planarHeight = 0;
    planarBytes = 0;
}
void bindPlanar(const RenderFrame &frame) {
    bool active =
        !reflecting && frame.planar.enabled && frame.planar.strength > 0 && bgfx::isValid(planarTarget);
    texture(15, "s_gmPlanar", active ? bgfx::getTexture(planarTarget) : black);
    vec("u_gmPlanar",
        {active ? frame.planar.strength : 0, planarWidth ? 1.f / planarWidth : 1,
         planarHeight ? 1.f / planarHeight : 1, bgfx::getCaps()->originBottomLeft ? .5f : -.5f});
    vec("u_gmPlanarClip", {reflecting ? 1.f : 0.f, frame.planar.height, 0, 0});
    for (int i = 0; i < 4; ++i)
        vec("u_gmPlanarRow" + std::to_string(i),
            {planarVp[i], planarVp[i + 4], planarVp[i + 8], planarVp[i + 12]});
}
void planarPass(const RenderAssets &assets, const RenderFrame &frame, const ViewCamera3D *camera,
                bool instances, uint32_t background) {
    const auto &p = frame.planar;
    if (!p.enabled || p.strength <= 0 || (!frame.cameraEnabled && !camera)) {
        planarClear();
        return;
    }
    uint16_t w = uint16_t(std::max(1.f, width * p.resolutionScale)),
             h = uint16_t(std::max(1.f, height * p.resolutionScale));
    if (w != planarWidth || h != planarHeight) {
        planarClear();
        const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        auto df = bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT)
                      ? bgfx::TextureFormat::D24S8
                      : bgfx::TextureFormat::D16;
        bgfx::TextureHandle attachments[] = {
            bgfx::createTexture2D(w, h, false, 1,
                                  hdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8, flags),
            bgfx::createTexture2D(w, h, false, 1, df, BGFX_TEXTURE_RT)};
        if (!bgfx::isValid(attachments[0]) || !bgfx::isValid(attachments[1])) {
            for (auto &a : attachments)
                releaseHandle(a);
            throw std::runtime_error("Planar reflection allocation failed");
        }
        planarTarget = bgfx::createFrameBuffer(2, attachments, true);
        if (!bgfx::isValid(planarTarget)) {
            for (auto &a : attachments)
                releaseHandle(a);
            throw std::runtime_error("Planar reflection framebuffer failed");
        }
        planarWidth = w;
        planarHeight = h;
        planarBytes = uint64_t(w) * h * ((hdr ? 8 : 4) + (df == bgfx::TextureFormat::D16 ? 2 : 4));
    }
    ViewCamera3D mirror{camera ? camera->transform : frame.camera, camera ? camera->lens : frame.lens};
    for (int i = 0; i < 3; ++i)
        mirror.transform[i * 4 + 1] *= -1;
    mirror.transform[13] = 2 * p.height - mirror.transform[13];
    auto originalProjection = qualityProjection;
    qualityProjection[8] = qualityProjection[9] = 0;
    planarVp = multiply3D(qualityProjection, inverseAffine3D(mirror.transform));
    bool originalDepth = depthPrepared, originalAo = aoActive;
    depthPrepared = false;
    aoActive = false;
    reflecting = true;
    auto originalTarget = target;
    auto originalWidth = width, originalHeight = height;
    target = planarTarget;
    width = w;
    height = h;
    bool hasSky = sky(assets, frame, &mirror);
    target = originalTarget;
    width = originalWidth;
    height = originalHeight;
    auto view = r.nextView();
    bgfx::setViewName(view, "GM planar reflection");
    bgfx::setViewFrameBuffer(view, planarTarget);
    bgfx::setViewRect(view, 0, 0, w, h);
    // Background is usually covered by the reflected sky. Alpha remains opaque for geometry.
    auto linear = [](uint32_t b) {
        float f = float(b) / 255;
        return f <= .04045f ? f / 12.92f : std::pow((f + .055f) / 1.055f, 2.4f);
    };
    float clear[] = {linear(background & 255), linear((background >> 8) & 255),
                     linear((background >> 16) & 255), 1};
    auto palette = r.clearColor(clear);
    if (hasSky)
        bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, uint32_t(0), 1.f, 0);
    else
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 1.f, 0, palette);
    bgfx::touch(view);
    auto &s = r.frameStats();
    auto oldMeshes = s.meshDraws, oldTriangles = s.triangles, oldTotal = s.totalMeshes,
         oldVisible = s.visibleMeshes;
    draw(assets, frame, view, instances, &mirror);
    s.reflectionDraws += s.meshDraws - oldMeshes;
    s.reflectionTriangles += s.triangles - oldTriangles;
    s.meshDraws = oldMeshes;
    s.triangles = oldTriangles;
    s.totalMeshes = oldTotal;
    s.visibleMeshes = oldVisible;
    qualityProjection = originalProjection;
    depthPrepared = originalDepth;
    aoActive = originalAo;
    reflecting = false;
}
