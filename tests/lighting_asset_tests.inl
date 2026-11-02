gm::HdrImage hdrImage{8, 4, std::vector<float>(8 * 4 * 3, 2.f)};
for (bool diffuse : {false, true}) {
    auto env = gm::cookEnvironment(hdrImage, diffuse, 8);
    check(!env.srgb && env.variants[0].format == gm::TextureFormat::Rgba16F, "HDR was converted to LDR/sRGB");
    check(env.levels == (diffuse ? 1 : 4), "Environment mip levels");
    const auto &bytes = env.variants[0].bytes;
    for (size_t i = 0; i < bytes.size(); i += 8)
        for (int c = 0; c < 3; ++c) {
            auto h = uint16_t(bytes[i + c * 2]) | (uint16_t(bytes[i + c * 2 + 1]) << 8);
            check(h == 0x4000, "Constant environment energy lost during convolution");
        }
    gm::RenderAssets envBank;
    envBank.textures = {env};
    auto encoded = gm::encodeRenderAssets(envBank);
    auto decoded = gm::decodeRenderAssets(encoded);
    check(decoded.textures[0].variants[0].bytes == bytes, "HDR binary roundtrip");
    gm::targetRenderAssets(decoded, "android");
    check(decoded.textures[0].variants.size() == 1, "HDR lost from Android target");
    envBank.textures[0].srgb = true;
    rejects([&] { gm::validateRenderAssets(envBank); });
    envBank.textures[0].srgb = false;
    envBank.textures[0].variants[0].bytes[1] = 0x7c;
    rejects([&] { gm::validateRenderAssets(envBank); });
}
hdrImage.rgb[0] = std::numeric_limits<float>::infinity();
rejects([&] { gm::cookEnvironment(hdrImage, false, 8); });
hdrImage.rgb[0] = 2;
rejects([&] { gm::cookEnvironment(hdrImage, false, 7); });
{
    std::ofstream hdrFile(root / "constant.hdr", std::ios::binary);
    hdrFile << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    const unsigned char pixel[4] = {128, 128, 128, 130};
    hdrFile.write(reinterpret_cast<const char *>(pixel), 4);
    hdrFile.write(reinterpret_cast<const char *>(pixel), 4);
}
auto imported = gm::importHdr(root / "constant.hdr");
check(imported.rgb[0] == 2.f, "HDR decode lost dynamic range");
gm::writeJsonAtomic(root / "env.texture.json", {{"format", "gm.texture"},
                                                {"version", 1},
                                                {"source", "constant.hdr"},
                                                {"environment", "radiance"},
                                                {"width", 8}});
auto envScene = gm::newScene();
envScene["entities"] =
    gm::Json::array({entity("environment", {component("EnvironmentLight", {{"radiance", "env.texture.json"}}),
                                            component("ShadowSettings"), component("IndirectLight")})});
auto envAsset = gm::compileScene(envScene, root);
gm::SceneRuntime envRun(gm::decodeScene(gm::encodeScene(envAsset)));
check(envRun.rendering().environment.radiance == 0 && envRun.rendering().shadows.enabled &&
          envRun.rendering().indirect.enabled,
      "Lighting component extraction");
auto envValue = gm::sdk::EnvironmentLight{};
envValue.radiance = 999;
auto envWords = gm::gameRegistry()->encode(gm::sdk::id("EnvironmentLight"), &envValue);
rejects([&] {
    envRun.set(envRun.entity(envRun.find("environment")), gm::sdk::id("EnvironmentLight"), envWords);
});
auto prefabEnv = envRun.loadPrefab(gm::encodeScene(envAsset));
auto instanceEnv = envRun.instantiatePrefab(prefabEnv);
check(envRun.asset().rendering.textures.size() == 2, "Environment prefab bank append");
auto instanceFields =
    envRun.component(envRun.find(envRun.entityId(instanceEnv)), gm::sdk::id("EnvironmentLight"));
gm::sdk::EnvironmentLight remappedEnvironment;
gm::gameRegistry()->decode(gm::sdk::id("EnvironmentLight"), instanceFields, &remappedEnvironment);
check(remappedEnvironment.radiance == 1, "Prefab environment texture index was not remapped");
// Scene data, editor registration and binary Player loading share these components.
auto localScene=gm::newScene();
localScene["entities"]=gm::Json::array({
    entity("point",{component("Transform3D",{{"x",3},{"y",2}}),component("PointLight",{{"intensity",42},{"range",7}})}),
    entity("atmosphere",{component("HeightFog",{{"density",.04},{"baseHeight",2}}),
                         component("PlanarReflection",{{"height",1},{"resolutionScale",.25}})})});
auto localAsset=gm::compileScene(localScene,root);
gm::SceneRuntime localRun(gm::decodeScene(gm::encodeScene(localAsset)));
const auto &localFrame=localRun.rendering();
check(localFrame.points.size()==1&&localFrame.points[0].position[0]==3&&localFrame.points[0].position[1]==2&&
      localFrame.points[0].light.intensity==42&&localFrame.points[0].light.range==7,"Point light scene extraction");
check(localFrame.fog.density==.04f&&localFrame.fog.baseHeight==2&&localFrame.planar.height==1&&
      localFrame.planar.resolutionScale==.25f,"Atmosphere/reflection binary roundtrip");
localScene["entities"][0]["components"][1]["fields"]["range"]=-1;
rejects([&]{gm::compileScene(localScene,root);});
check(gm::normalizedMaterial({{"format","gm.material"},{"version",1},{"parameters",{{"u_planar",{1,0,0,0}}}}})
          ["parameters"].contains("u_planar"),"Builtin planar material source rejected");
auto projectionFrame = gm::RenderFrame{};
projectionFrame.shadows = {};
projectionFrame.light = {0, -1, 0};
gm::RenderAssets projectionAssets;
projectionAssets.meshes = {gm::primitiveMesh("builtin:cube")};
projectionFrame.meshes = {{1, 0, gm::NoIndex, gm::local3D({0, 50, 0})}};
auto projection = gm::shadowProjection(projectionAssets, projectionFrame, gm::identity3D(), 1024, false);
check(gm::visibleBounds3D(gm::multiply3D(projection.clip, projectionFrame.meshes[0].transform),
                          {-.5f, -.5f, -.5f}, {.5f, .5f, .5f}, false),
      "Off-camera caster omitted from shadow depth");
auto movedEye = gm::identity3D();
movedEye[12] = .00001f;
auto snapped = gm::shadowProjection(projectionAssets, projectionFrame, movedEye, 1024, false);
check(snapped.clip[12] == projection.clip[12] && snapped.clip[13] == projection.clip[13],
      "Shadow projection not texel snapped");
// SH diffuse convolution preserves a directional, low-frequency radiance field.
check(gm::normalizedMaterial({{"format", "gm.material"}, {"version", 1},
                              {"parameters", {{"u_specular", {.04, .04, .04, .5}}}}})
          ["parameters"]["u_specular"][3] == .5,
      "Builtin material discarded dielectric specular control");
gm::HdrImage gradientSky{64, 32, std::vector<float>(64 * 32 * 3)};
for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 64; ++x)
        for (int c = 0; c < 3; ++c)
            gradientSky.rgb[(y * 64 + x) * 3 + c] = 2.f + std::cos((y + .5f) * 3.14159265f / 32);
auto diffuseSky = gm::cookEnvironment(gradientSky, true, 32);
auto halfValue = [](const std::vector<uint8_t> &bytes, size_t offset) {
    uint16_t h = uint16_t(bytes[offset]) | uint16_t(bytes[offset + 1]) << 8;
    return std::ldexp(1.f + float(h & 1023) / 1024, int((h >> 10) & 31) - 15);
};
for (uint32_t y = 0; y < 16; ++y) {
    float expected = 2.f + (2.f / 3) * std::cos((y + .5f) * 3.14159265f / 16);
    check(std::abs(halfValue(diffuseSky.variants[0].bytes, size_t(y) * 32 * 8) - expected) < .01f,
          "Diffuse environment lost directional energy");
}
