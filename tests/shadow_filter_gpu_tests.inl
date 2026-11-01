// A visibility-only receiver isolates the filter from material/highlight variation.
// Nearest-point 3x3 PCF has at most ten distinct levels; bilinear PCF must interpolate.
world.clear();
assets = {};
frame = {};
assets.meshes = {gm::primitiveMesh("builtin:cube"), gm::primitiveMesh("builtin:plane")};
gm::ShaderVariant visibilityShader;
visibilityShader.vertex.assign(std::begin(vs_mesh_dxbc), std::end(vs_mesh_dxbc));
visibilityShader.fragment.assign(std::begin(fs_shadow_visibility_dxbc), std::end(fs_shadow_visibility_dxbc));
assets.shaders = {{{visibilityShader}}};
gm::CookedMaterial visibilityMaterial;
visibilityMaterial.shader = 0;
assets.materials = {visibilityMaterial};
frame.cameraEnabled = true;
frame.camera = gm::local3D({0, 5, -8, 30});
frame.light = {.6f, -1, .2f, 0xffffffff, 3, 0, 1};
frame.meshes = {{1, 1, 0, gm::local3D({0, 0, 0, 0, 0, 0, 12, 1, 12})},
                {2, 0, 0, gm::local3D({0, 1, 0, 0, 0, 0, 2, 2, 2})}};
frame.post = {};
frame.post.bloom = frame.post.fxaa = frame.post.toneMapping = 0;
frame.post.gamma = 1;
frame.shadows = {1, 256, 16, .0003f, .01f, 1};
for (float radius : {0.f, 1.f, 2.f}) {
    frame.shadows.softness = radius;
    auto filterPixels = snapshot(draw);
    std::array<bool, 256> levels{};
    for (int y = 170; y < 450; ++y)
        for (int x = 120; x < 520; ++x)
            levels[filterPixels[(y * 640 + x) * 4]] = true;
    auto count = std::count(levels.begin() + 1, levels.end() - 1, true);
    check(count > 40, "Shadow filtering still quantizes comparison results into discrete bands");
    lodepng::encode((output / ("shadow-filter-" + std::to_string(int(radius)) + ".png")).string(),
                    filterPixels, 640, 480);
}
