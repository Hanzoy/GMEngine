// SceneRuntime::Impl member; entirely bypassed in a purely 2D World.
void transforms3D() {
    auto type = types.at(sdk::id(sdk::WorldTransform3D::gm_name)).id;
    bool present = ecs_count_id(world.c_ptr(), types.at(sdk::id(sdk::Transform3D::gm_name)).id) > 0;
    if (!present && world3D.empty())
        return;
    if (!present) {
        for (auto i : order)
            ecs_remove_id(world.c_ptr(), handles[i], type);
        world3D.clear();
        return;
    }
    world3D.resize(records.size(), identity3D());
    for (auto i : order) {
        auto e = handles[i];
        auto *local =
            static_cast<const sdk::Transform3D *>(raw(*this, e, sdk::id(sdk::Transform3D::gm_name)));
        auto matrix = local ? local3D(*local) : identity3D();
        auto parent = records[i].parent;
        if (parent != NoIndex)
            matrix = multiply3D(world3D[parent], matrix);
        world3D[i] = matrix;
        auto *existing = const_cast<sdk::WorldTransform3D *>(static_cast<const sdk::WorldTransform3D *>(
            raw(*this, e, sdk::id(sdk::WorldTransform3D::gm_name))));
        if (local) {
            sdk::WorldTransform3D value;
            std::copy(matrix.begin(), matrix.end(), value.matrix);
            if (existing)
                *existing = value;
            else
                ecs_set_id(world.c_ptr(), e, type, sizeof(value), &value);
        } else if (existing)
            ecs_remove_id(world.c_ptr(), e, type);
    }
}
