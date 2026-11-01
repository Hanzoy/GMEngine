world.clear();
assets = {};
frame = {};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 4, -8, 26});
assets.meshes = {gm::primitiveMesh("builtin:plane")};
gm::CookedMaterial localGray;
localGray.parameters = {{"u_baseColor", {.6f, .6f, .6f, 1}}, {"u_surface", {0, .5f, 0, 0}}};
assets.materials = {localGray};
frame.meshes = {{1, 0, 0, gm::local3D({0, 0, 0, 0, 0, 0, 20, 1, 20})}};
frame.light.intensity = frame.light.ambient = 0;
frame.post = {};
frame.post.fxaa = 0;
auto noLocal = snapshot(draw);
frame.points = {{{0, 2, 0}, {0xff0000ff, 35, 8, 1}}};
auto redLocal = snapshot(draw);
int redPixels = 0;
for (size_t i = 0; i < redLocal.size(); i += 4)
    if (int(redLocal[i]) - redLocal[i + 2] > 25 && int(redLocal[i]) - noLocal[i] > 25)
        ++redPixels;
check(redPixels > 500, "Point light failed to illuminate nearby geometry in its color");
frame.points[0].light.range = .1f;
check(snapshot(draw) == noLocal, "Point light affected geometry outside its range");
frame.points[0].light.range = 8;
frame.points[0].light.enabled = 0;
check(snapshot(draw) == noLocal, "Disabled point light still illuminates geometry");
frame.points.clear();
frame.fog = {0xffff2000, .5f, 0, 0, 0, 1, 1};
auto fogged = snapshot(draw);
int bluePixels = 0;
for (size_t i = 0; i < fogged.size(); i += 4)
    if (int(fogged[i + 2]) - fogged[i] > 40)
        ++bluePixels;
check(bluePixels > 10000, "Height fog failed to tint distant geometry");
frame.fog.enabled = 0;
check(snapshot(draw) == noLocal, "Disabled height fog altered scene colors");
frame.quality = {};
frame.quality.ambientOcclusion = 0;
snapshot(draw);
frame.points = {{{0, 2, 0}, {0xff0000ff, 35, 8, 1}}};
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().temporalResets == 1, "Changed point lighting retained stale TAA history");
snapshot(draw);
frame.fog.enabled = 1;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().temporalResets == 1, "Changed fog retained stale TAA history");
// Animated transparent weather does not cast shadows and must not invalidate their cache/depth fit.
world.clear();assets.materials.push_back(localGray);
assets.materials.back().blend=gm::BlendMode::Alpha;assets.materials.back().depthWrite=false;
frame.meshes.push_back({2,0,1,gm::local3D({0,50,0})});
frame.light.intensity=1;frame.shadows={1,512,20};frame.indirect={1,128,16,3,1};
snapshot(draw);
frame.meshes.back().transform[13]=-100;
renderer.begin(640,480);draw();renderer.end();
check(renderer.stats().lightingCacheHits==1&&renderer.stats().shadowDraws==0&&renderer.stats().rsmDraws==0,
      "Non-casting transparent movement rebuilt shadow/RSM maps");
