// AO must affect ambient contact regions while leaving direct light/emission intact.
world.clear();
assets = {};
frame = {};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 4, -8, 22});
assets.meshes = {gm::primitiveMesh("builtin:cube"), gm::primitiveMesh("builtin:plane")};
gm::CookedMaterial gray;
gray.parameters = {{"u_baseColor", {.65f, .65f, .65f, 1}}, {"u_surface", {0, .8f, 0, 0}}};
assets.materials = {gray};
frame.meshes = {{1, 1, 0, gm::local3D({0, 0, 0, 0, 0, 0, 12, 1, 12})},
                {2, 0, 0, gm::local3D({0, 1, 0, 0, 0, 0, 2, 2, 2})}};
frame.light.intensity = 0;
frame.light.ambient = .4f;
frame.post = {};
frame.post.fxaa = 0;
auto ambientOnly = snapshot(draw);
frame.quality = {};
frame.quality.temporalAA = 0;
auto withAo = snapshot(draw);
int contactPixels = 0;
for (size_t i = 0; i < withAo.size(); i += 4)
    if (int(ambientOnly[i]) - withAo[i] > 8)
        ++contactPixels;
check(contactPixels > 300, "AO failed to darken ambient contacts");
check(renderer.stats().depthPrepassDraws == 2 && renderer.stats().aoPasses == 1,
      "AO depth/resolve accounting");
lodepng::encode((output / "quality-ao-off.png").string(), ambientOnly, 640, 480);
lodepng::encode((output / "quality-ao-on.png").string(), withAo, 640, 480);
frame.light.intensity = 3;
frame.light.ambient = 0;
auto directAo = snapshot(draw);
frame.quality.enabled = 0;
auto directNoAo = snapshot(draw);
check(directAo == directNoAo, "AO incorrectly darkened direct lighting");
frame.quality = {};
frame.quality.ambientOcclusion = 0;
snapshot(draw);
check(renderer.stats().temporalPasses == 1 && renderer.stats().temporalResets == 0,
      "Temporal history did not accumulate");
frame.camera[12] += .01f;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().temporalResets == 0, "Camera motion unnecessarily discarded temporal history");
frame.meshes[1].transform[12] += 1;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().temporalResets == 1, "Moving object retained stale temporal history");
frame.camera[12] += 5;
renderer.begin(640, 480);
draw();
renderer.end();
check(renderer.stats().temporalResets == 1, "Camera cut retained stale temporal history");
for (int i = 0; i < 40; ++i) {
    renderer.begin(640, 480);
    draw();
    renderer.end();
}
auto settled = snapshot(draw);
check(renderer.stats().temporalCacheHits == 1, "Static temporal history did not settle");
check(snapshot(draw) == settled, "Converged static image still flickers");
world.temporalAllowed(false);
snapshot(draw);
check(renderer.stats().temporalPasses == 0, "Mixed 2D/3D temporal bypass failed");
world.temporalAllowed(true);
frame.quality.enabled = 0;
snapshot(draw);
check(renderer.stats().qualityBytes == 0 && renderer.stats().temporalPasses == 0 &&
          renderer.stats().aoPasses == 0,
      "Disabled quality effects retained targets");
frame.quality = {0};
