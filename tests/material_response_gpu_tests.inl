// Material response contracts: direct, local and environment paths must agree.
world.clear();
assets = {};
frame = {};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 0, -4});
frame.post = {};
frame.post.fxaa = 0;
frame.quality = {0};
assets.meshes = {gm::primitiveMesh("builtin:cube")};
gm::CookedMaterial dielectric;
dielectric.parameters = {{"u_baseColor", {0, 0, 0, 1}},
                         {"u_surface", {0, .35f, 0, 0}},
                         {"u_specular", {.04f, .04f, .04f, 1}}};
assets.materials = {dielectric};
frame.meshes = {{1, 0, 0, gm::identity3D()}};
frame.light = {0, 0, 1, 0xffffffff, 3, 0, 1};
const size_t materialCenter = (240 * 640 + 320) * 4;
auto control = [&](float weight) {
    assets.materials[0].parameters[2].value[3] = weight;
    return snapshot(draw);
};
auto directFull = control(1), directHalf = control(.5f), directZero = control(0);
check(directFull[materialCenter] > directHalf[materialCenter] + 10 &&
          directHalf[materialCenter] > directZero[materialCenter] + 10 && directZero[materialCenter] < 2,
      "Dielectric weight failed for directional highlights");
frame.light.intensity = 0;
frame.points = {{{0, 0, -3}, {0xffffffff, 20, 10, 1}}};
auto pointFull = control(1), pointZero = control(0);
check(pointFull[materialCenter] > 30 && pointZero[materialCenter] < 2,
      "Dielectric weight failed for local highlights");
frame.points.clear();
gm::HdrImage materialSky{8, 4, std::vector<float>(8 * 4 * 3, 2.f)};
assets.textures = {gm::cookEnvironment(materialSky, true, 8), gm::cookEnvironment(materialSky, false, 8)};
world.clear();
frame.environment = {1, 0, 1, 1, 0, 0};
auto envFull = control(1), envZero = control(0);
check(envFull[materialCenter] > 20 && envZero[materialCenter] < 2,
      "Zero specular left environment reflections (DFG grazing term)");
frame.meshes[0].transform = gm::local3D({0, 0, 0, 0, 65});
auto grazingZero = control(0);
check(grazingZero[materialCenter] < 2, "Zero specular regained a grazing-angle lobe");
frame.meshes[0].transform = gm::identity3D();
// Conductor reflection is unchanged by the dielectric-only control.
assets.materials[0].parameters[0].value = {.8f, .3f, .1f, 1};
assets.materials[0].parameters[1].value[0] = 1;
frame.light.intensity = 2;
frame.points = {{{0, 0, -3}, {0xffffffff, 10, 10, 1}}};
check(control(1) == control(0), "Dielectric control incorrectly attenuated metals");
// Missing parameters use defaults; a previous zero-weight draw must not leak.
assets.materials[0].parameters[0].value = {0, 0, 0, 1};
assets.materials[0].parameters[1].value[0] = 0;
auto explicitDefault = control(1);
control(0);
assets.materials[0].parameters.pop_back();
check(snapshot(draw) == explicitDefault, "Default specular material changed or leaked state");
// A constant tangent-space normal must be independent of UV shear.
world.clear();
assets = {};
frame = {};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 4, -6, 33});
frame.post = {};
frame.post.fxaa = 0;
frame.light = {.4f, -1, .5f, 0xffffffff, 2, 0, 1};
frame.quality = {0};
assets.meshes = {gm::primitiveMesh("builtin:plane")};
gm::TextureSettings tangentSettings;
tangentSettings.srgb = false;
tangentSettings.formats = {gm::TextureFormat::Rgba8};
assets.textures = {gm::cookTexture({1, 1, {128, 220, 216, 255}}, tangentSettings)};
dielectric.parameters = {{"u_baseColor", {.4f, .4f, .4f, 1}}, {"u_surface", {0, .7f, 0, 1}},
                         {"u_specular", {.04f, .04f, .04f, 0}}};
dielectric.samplers = {{"s_normal", 0, 1}};
assets.materials = {dielectric};
frame.meshes = {{1, 0, 0, gm::local3D({0, 0, 0, 0, 0, 0, 5, 1, 5})}};
auto regularUv = snapshot(draw);
for (auto &vertex : assets.meshes[0].vertices)
    vertex.u += .8f * vertex.v;
world.clear();
auto skewedUv = snapshot(draw);
int tangentDelta = 0;
for (size_t i = 0; i < regularUv.size(); ++i)
    tangentDelta = std::max(tangentDelta, std::abs(int(regularUv[i]) - int(skewedUv[i])));
check(tangentDelta <= 1, "UV shear distorted the orthonormal normal-map basis");
lodepng::encode((output / "material-tangent-frame.png").string(), skewedUv, 640, 480);
