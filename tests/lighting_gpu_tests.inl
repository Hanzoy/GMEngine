// Real framebuffer comparisons: effects must change receivers, not just add submissions.
world.clear();
viewport = {0, 0, 640, 480};
assets = {};
frame = {};
frame.cameraEnabled = true;
assets.meshes = {gm::primitiveMesh("builtin:cube"), gm::primitiveMesh("builtin:plane")};
gm::CookedMaterial stone, wall;
stone.parameters = {{"u_baseColor", {.65f, .65f, .65f, 1}}, {"u_surface", {0, .8f, 0, 0}}};
wall.parameters = {{"u_baseColor", {.8f, .025f, .01f, 1}}, {"u_surface", {0, .8f, 0, 0}}};
assets.materials = {stone, wall};
frame.camera = gm::local3D({0, 4, -8, 22});
frame.light = {.6f, -1, .2f, 0xffffffff, 3, .015f, 1};
frame.post = {};
frame.post.fxaa = 0;
frame.meshes = {{1, 1, 0, gm::local3D({0, 0, 0, 0, 0, 0, 12, 1, 12})},
                {2, 0, 1, gm::local3D({0, 1, 0, 0, 0, 0, 2, 2, 2})}};
auto noShadow = snapshot(draw);
frame.shadows = {1, 1024, 16, .0003f, .01f, 1};
auto shadowed = snapshot(draw);
int darker = 0;
for (size_t i = 0; i < noShadow.size(); i += 4)
    if (int(noShadow[i]) - shadowed[i] > 20)
        ++darker;
check(darker > 1500, "Shadow map failed to darken floor receivers");
check(renderer.stats().shadowDraws == 0 && renderer.stats().lightingCacheHits == 1 &&
          renderer.stats().lightingBytes > 0,
      "Shadow accounting");
lodepng::encode((output / "lighting-shadow-off.png").string(), noShadow, 640, 480);
lodepng::encode((output / "lighting-shadow-on.png").string(), shadowed, 640, 480);
// Alpha-cutout casters must not turn invisible texels into solid shadows.
world.clear();
gm::TextureSettings cutoutSettings;
cutoutSettings.formats = {gm::TextureFormat::Rgba8};
assets.textures = {gm::cookTexture({1, 1, {255, 255, 255, 0}}, cutoutSettings)};
assets.materials[1].samplers = {{"s_baseColor", 0, 0}};
assets.materials[1].parameters[1].value[2] = .5f;
auto cutout = snapshot(draw);
frame.meshes.pop_back();
auto empty = snapshot(draw);
check(cutout == empty, "Alpha-cutout left a solid shadow");
assets.materials[1].samplers.clear();
assets.materials[1].parameters[1].value[2] = 0;
frame.meshes.push_back({2, 0, 1, gm::local3D({0, 1, 0, 0, 0, 0, 2, 2, 2})});
frame.indirect = {1, 256, 64, 5, 1};
auto bounced = snapshot(draw);
int redBounce = 0;
for (size_t i = 0; i < bounced.size(); i += 4) {
    int dr = int(bounced[i]) - shadowed[i], dg = int(bounced[i + 1]) - shadowed[i + 1];
    if (dr > 3 && dr > dg + 2)
        ++redBounce;
}
check(redBounce > 200, "RSM failed to transfer red surface color to receiver");
check(renderer.stats().rsmDraws == 0 && renderer.stats().lightingCacheHits == 1, "Static RSM cache");
frame.meshes[1].transform[12] += 1;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().shadowDraws == 2 && renderer.stats().rsmDraws == 2 &&
          !renderer.stats().lightingCacheHits,
      "Moved caster did not invalidate lighting");
frame.meshes[1].transform[12] -= 1;
check(snapshot(draw) == bounced, "Cached lighting differs from rebuilt lighting");
frame.light.intensity += .01f;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().rsmDraws == 2, "Changed light did not invalidate RSM");
frame.light.intensity -= .01f;
lodepng::encode((output / "lighting-bounce-on.png").string(), bounced, 640, 480);
frame.indirect.enabled = 0;
frame.shadows.enabled = 0;
snapshot(draw);
check(!renderer.stats().lightingBytes && !renderer.stats().shadowDraws && !renderer.stats().rsmDraws,
      "Disabled lighting retained GPU targets");
// No direct light: a fully metallic object still reflects HDR environment radiance.
world.clear();
assets.textures.clear();
gm::HdrImage constant{8, 4, std::vector<float>(8 * 4 * 3)};
for (size_t i = 0; i < constant.rgb.size(); i += 3) {
    constant.rgb[i] = .1f;
    constant.rgb[i + 1] = .2f;
    constant.rgb[i + 2] = 2;
}
assets.textures = {gm::cookEnvironment(constant, true, 8), gm::cookEnvironment(constant, false, 8)};
assets.materials[0].parameters = {{"u_baseColor", {1, 1, 1, 1}}, {"u_surface", {1, .2f, 0, 0}}};
frame.light.intensity = frame.light.ambient = 0;
frame.camera = gm::local3D({0, 0, -5});
frame.meshes = {{1, 0, 0, gm::identity3D()}};
frame.environment = {1, 0, 1, 1, 0, 0};
auto ibl = snapshot(draw);
auto centerPixel = (240 * 640 + 320) * 4;
check(ibl[centerPixel + 2] > ibl[centerPixel] + 50 && ibl[centerPixel + 2] > 150,
      "HDR specular environment missing");
frame.environment.enabled = 0;
auto blackMetal = snapshot(draw);
check(blackMetal[centerPixel + 2] < 5, "Disabled environment uniform leaked");
frame.environment.enabled = frame.environment.background = 1;
auto skyPixels = snapshot(draw);

check(skyPixels[(100 * 640 + 10) * 4 + 2] > 150 && renderer.stats().environmentDraws == 1,
      "HDR sky background missing");
lodepng::encode((output / "lighting-hdr-environment.png").string(), skyPixels, 640, 480);
world.clear();
for (size_t i = 0; i < constant.rgb.size(); i += 3) {
    bool left = (i / 3) % 8 < 4;
    constant.rgb[i] = left ? .1f : 2.f;
    constant.rgb[i + 2] = left ? 2.f : .1f;
}
assets.textures = {gm::cookEnvironment(constant, true, 8), gm::cookEnvironment(constant, false, 8)};
frame.environment.background = 0;
auto rotationZero = snapshot(draw);
frame.environment.rotation = 180;
auto rotationHalf = snapshot(draw);
check(rotationZero[centerPixel + 2] > rotationHalf[centerPixel + 2] + 40 &&
          rotationHalf[centerPixel] > rotationZero[centerPixel] + 40,
      "Environment rotation did not rotate reflections");
frame.environment = {0};
frame.post = {};
