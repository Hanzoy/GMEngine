// Included within game_registry.cpp's anonymous namespace.
const Field transform3Fields[] = {GM_FLOAT(Transform3D, x, "位置 X", 0, -100000, 100000),
                                  GM_FLOAT(Transform3D, y, "位置 Y", 0, -100000, 100000),
                                  GM_FLOAT(Transform3D, z, "位置 Z", 0, -100000, 100000),
                                  GM_FLOAT(Transform3D, rotationX, "旋转 X", 0, -360000, 360000),
                                  GM_FLOAT(Transform3D, rotationY, "旋转 Y", 0, -360000, 360000),
                                  GM_FLOAT(Transform3D, rotationZ, "旋转 Z", 0, -360000, 360000),
                                  GM_FLOAT(Transform3D, scaleX, "缩放 X", 1, .001, 1000),
                                  GM_FLOAT(Transform3D, scaleY, "缩放 Y", 1, .001, 1000),
                                  GM_FLOAT(Transform3D, scaleZ, "缩放 Z", 1, .001, 1000)};
const char *transform3Dependency[] = {"Transform3D"};
const Field meshFields[] = {
    {"mesh", "网格", Kind::Mesh, offsetof(MeshRenderer, mesh), UINT32_MAX, 0, UINT32_MAX},
    {"material", "材质", Kind::Material, offsetof(MeshRenderer, material), UINT32_MAX, 0, UINT32_MAX},
    GM_BOOL(MeshRenderer, enabled, "启用", 1)};
const Field camera3Fields[] = {GM_FLOAT(Camera3D, fieldOfView, "视野角", 60, 5, 150),
                               GM_FLOAT(Camera3D, nearPlane, "近裁面", .1, .001, 1000),
                               GM_FLOAT(Camera3D, farPlane, "远裁面", 1000, .01, 100000),
                               GM_BOOL(Camera3D, enabled, "启用", 1),
                               GM_INT(Camera3D, priority, "优先级", 0, -1000, 1000)};
bool validCamera3(const void *value, char *error, uint32_t capacity) {
    auto &c = *static_cast<const Camera3D *>(value);
    if (c.farPlane > c.nearPlane)
        return true;
    if (capacity)
        std::snprintf(error, capacity, "farPlane must exceed nearPlane");
    return false;
}
const Field lightFields[] = {
    GM_FLOAT(DirectionalLight, directionX, "方向 X", -.4, -1, 1),
    GM_FLOAT(DirectionalLight, directionY, "方向 Y", -1, -1, 1),
    GM_FLOAT(DirectionalLight, directionZ, "方向 Z", .3, -1, 1),
    {"color", "颜色", Kind::Color, offsetof(DirectionalLight, color), 0xffffffff, 0, UINT32_MAX},
    GM_FLOAT(DirectionalLight, intensity, "强度", 3, 0, 100),
    GM_FLOAT(DirectionalLight, ambient, "环境光", .08, 0, 2),
    GM_BOOL(DirectionalLight, enabled, "启用", 1)};
bool validLight(const void *value, char *error, uint32_t capacity) {
    auto &l = *static_cast<const DirectionalLight *>(value);
    if (l.directionX * l.directionX + l.directionY * l.directionY + l.directionZ * l.directionZ > 1e-8f)
        return true;
    if (capacity)
        std::snprintf(error, capacity, "Light direction cannot be zero");
    return false;
}
const Field postFields[] = {GM_BOOL(PostProcess, enabled, "启用", 1),
                            GM_FLOAT(PostProcess, exposure, "曝光", 1, .01, 16),
                            GM_FLOAT(PostProcess, gamma, "Gamma", 2.2, 1, 3),
                            GM_BOOL(PostProcess, toneMapping, "色调映射", 1),
                            GM_BOOL(PostProcess, bloom, "Bloom", 0),
                            GM_FLOAT(PostProcess, bloomThreshold, "Bloom 阈值", 1, 0, 20),
                            GM_FLOAT(PostProcess, bloomIntensity, "Bloom 强度", .15, 0, 4),
                            GM_FLOAT(PostProcess, bloomRadius, "Bloom 半径", 1, .1, 4),
                            GM_BOOL(PostProcess, fxaa, "FXAA", 1),
                            GM_FLOAT(PostProcess, renderScale, "渲染比例", 1, .25, 1)};
bool renderComponent(sdk::TypeId type) {
    return type == id(Transform3D::gm_name) || type == id(MeshRenderer::gm_name) ||
           type == id(Camera3D::gm_name) || type == id(DirectionalLight::gm_name) ||
           type == id(PostProcess::gm_name);
}

const Field shadowFields[] = {GM_BOOL(ShadowSettings, enabled, "启用", 1),
                              GM_INT(ShadowSettings, resolution, "分辨率", 2048, 256, 4096),
                              GM_FLOAT(ShadowSettings, distance, "覆盖范围（米）", 60, 1, 1000),
                              GM_FLOAT(ShadowSettings, depthBias, "深度偏移", .0005, 0, .02),
                              GM_FLOAT(ShadowSettings, normalBias, "法线偏移（米）", .02, 0, 1),
                              GM_FLOAT(ShadowSettings, softness, "PCF 半径（像素）", 1, 0, 3)};
const Field environmentFields[] = {GM_BOOL(EnvironmentLight, enabled, "启用", 1),
                                   {"irradiance", "漫反射环境贴图", Kind::Texture,
                                    offsetof(EnvironmentLight, irradiance), UINT32_MAX, 0, UINT32_MAX},
                                   {"radiance", "预滤波反射贴图", Kind::Texture,
                                    offsetof(EnvironmentLight, radiance), UINT32_MAX, 0, UINT32_MAX},
                                   GM_FLOAT(EnvironmentLight, intensity, "强度", 1, 0, 20),
                                   GM_FLOAT(EnvironmentLight, rotation, "旋转 Y", 0, -360000, 360000),
                                   GM_BOOL(EnvironmentLight, background, "天空背景", 1)};
const Field indirectFields[] = {GM_BOOL(IndirectLight, enabled, "启用", 1),
                                GM_INT(IndirectLight, resolution, "RSM 分辨率", 256, 64, 1024),
                                GM_INT(IndirectLight, samples, "采样数", 32, 8, 64),
                                GM_FLOAT(IndirectLight, radius, "反弹半径（米）", 5, .1, 50),
                                GM_FLOAT(IndirectLight, intensity, "强度", 1, 0, 8)};
bool validShadows(const void *value, char *error, uint32_t capacity) {
    auto &s = *static_cast<const ShadowSettings *>(value);
    if ((s.resolution & (s.resolution - 1)) == 0)
        return true;
    if (capacity)
        std::snprintf(error, capacity, "Shadow resolution must be a power of two");
    return false;
}
bool validIndirect(const void *value, char *error, uint32_t capacity) {
    auto &s = *static_cast<const IndirectLight *>(value);
    if ((s.resolution & (s.resolution - 1)) == 0)
        return true;
    if (capacity)
        std::snprintf(error, capacity, "RSM resolution must be a power of two");
    return false;
}
bool lightingComponent(sdk::TypeId type) {
    return type == id(ShadowSettings::gm_name) || type == id(EnvironmentLight::gm_name) ||
           type == id(IndirectLight::gm_name);
}
const Field qualityFields[] = {GM_BOOL(RenderQuality, enabled, "启用", 1),
                               GM_BOOL(RenderQuality, temporalAA, "时间抗锯齿", 1),
                               GM_BOOL(RenderQuality, ambientOcclusion, "屏幕空间环境遮挡", 1),
                               GM_FLOAT(RenderQuality, historyWeight, "历史权重", .9, 0, .96),
                               GM_FLOAT(RenderQuality, aoRadius, "遮挡半径（米）", 1, .05, 5),
                               GM_FLOAT(RenderQuality, aoIntensity, "遮挡强度", 1.5, 0, 4),
                               GM_FLOAT(RenderQuality, aoBias, "遮挡偏移", .05, 0, .5)};
