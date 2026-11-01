void bindLocal(const RenderAssets &assets, const RenderFrame &frame, const MeshDraw &draw) {
    const auto &mesh = assets.meshes.at(draw.mesh);
    Vec3 half{}, center{};
    for (int i = 0; i < 3; ++i) {
        half[i] = (mesh.maximum[i] - mesh.minimum[i]) * .5f;
        center[i] = (mesh.maximum[i] + mesh.minimum[i]) * .5f;
    }
    center = transformPoint(draw.transform, center);
    float radius = 0;
    for (int i = 0; i < 3; ++i) {
        float length = 0;
        for (int j = 0; j < 3; ++j)
            length += draw.transform[i * 4 + j] * draw.transform[i * 4 + j];
        radius += half[i] * std::sqrt(length);
    }
    std::array<std::pair<float, const RenderFrame::Point *>, 8> chosen{};
    for (const auto &p : frame.points) {
        if (!p.light.enabled || p.light.intensity <= 0 || p.light.range <= 0)
            continue;
        float d2 = 0;
        for (int i = 0; i < 3; ++i) {
            float d = center[i] - p.position[i];
            d2 += d * d;
        }
        float d = std::max(0.f, std::sqrt(d2) - radius);
        if (d >= p.light.range)
            continue;
        float score = p.light.intensity / (1 + d2);
        for (size_t i = 0; i < chosen.size(); ++i)
            if (score > chosen[i].first) {
                for (size_t j = chosen.size() - 1; j > i; --j)
                    chosen[j] = chosen[j - 1];
                chosen[i] = {score, &p};
                break;
            }
    }
    for (size_t i = 0; i < chosen.size(); ++i) {
        std::array<float, 4> position{0, 0, 0, 1}, localColor{};
        if (auto p = chosen[i].second) {
            position = {p->position[0], p->position[1], p->position[2], p->light.range};
            localColor = {float(p->light.color & 255) / 255, float((p->light.color >> 8) & 255) / 255,
                     float((p->light.color >> 16) & 255) / 255, p->light.intensity};
        }
        vec("u_gmPointP" + std::to_string(i), position);
        vec("u_gmPointC" + std::to_string(i), localColor);
    }
    const auto &f = frame.fog;
    auto linear = [](uint32_t c) {
        float v = float(c) / 255;
        return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
    };
    vec("u_gmFogColor", {linear(f.color & 255), linear((f.color >> 8) & 255), linear((f.color >> 16) & 255),
                         f.enabled ? f.density : 0});
    vec("u_gmFog", {f.startDistance, f.heightFalloff, f.baseHeight, f.maxOpacity});
}
