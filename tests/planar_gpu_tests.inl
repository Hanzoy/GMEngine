world.clear();
assets = {};
frame = {};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 2, -6, 14});
assets.meshes = {gm::primitiveMesh("builtin:plane"), gm::primitiveMesh("builtin:cube")};
gm::CookedMaterial mirrorMaterial, redEmitter;
mirrorMaterial.parameters = {
    {"u_baseColor", {.02f, .02f, .02f, 1}}, {"u_surface", {0, .05f, 0, 0}}, {"u_planar", {1, 0, 0, 0}}};
redEmitter.parameters = {{"u_baseColor", {1, 0, 0, 1}}, {"u_unlit", {1, 0, 0, 0}}};
assets.materials = {mirrorMaterial, redEmitter};
frame.meshes = {{1, 0, 0, gm::local3D({0, 0, 0, 0, 0, 0, 12, 1, 12})}, {2, 1, 1, gm::local3D({0, 1.5f, 1})}};
frame.light.intensity = frame.light.ambient = 0;
frame.post = {};
frame.post.fxaa = 0;
auto withoutReflection = snapshot(draw);
frame.planar = {0, .5f, 1, 1};
auto withReflection = snapshot(draw);
lodepng::encode((output / "planar.png").string(), withReflection, 640, 480);
int reflectedPixels = 0;
for (size_t i = 0; i < withReflection.size(); i += 4)
    if (int(withReflection[i]) - withoutReflection[i] > 15 && withReflection[i] > withReflection[i + 1] + 15)
        ++reflectedPixels;
check(reflectedPixels > 100, "Planar reflection failed to reflect the red cube into the floor");
check(renderer.stats().reflectionDraws == 1, "Reflection receiver recursively rendered into its own target");
auto reflectionBytes = renderer.stats().reflectionBytes;
check(reflectionBytes > 0, "Reflection target memory not reported");
frame.planar.resolutionScale = .25f;
snapshot(draw);
check(renderer.stats().reflectionBytes * 4 == reflectionBytes, "Reflection resolution did not resize target");
frame.planar.enabled = 0;
check(snapshot(draw) == withoutReflection, "Disabled planar reflection changes original rendering");
check(renderer.stats().reflectionBytes == 0 && renderer.stats().reflectionDraws == 0,
      "Disabled reflection retains targets/passes");
frame.planar = {0, .5f, 1, 1};
frame.meshes[1].transform = gm::local3D({0, -1.5f, 1});
auto below = snapshot(draw);
int belowRed = 0;
for (size_t i = 0; i < below.size(); i += 4)
    if (int(below[i]) - below[i + 1] > 15)
        ++belowRed;
check(belowRed == 0, "Planar reflection included geometry below the reflecting plane");
