// Included after SceneRuntime::Impl, inside namespace gm.
uint64_t SceneRuntime::loadPrefab(std::span<const uint8_t> bytes) {
    if (impl_->prefabs.size() >= 64)
        throw std::runtime_error("Prefab definition limit reached");
    auto prefab = decodeScene(bytes);
    if (prefab.entities.empty() || prefab.entities[0].parent != NoIndex || prefab.hud.nodes.size() != 1 ||
        !prefab.bindings.empty() ||
        std::count_if(prefab.entities.begin(), prefab.entities.end(),
                      [](const SceneEntity &e) { return e.parent == NoIndex; }) != 1)
        throw std::runtime_error("Prefab requires one root at index zero and no HUD");
    auto images = asset_.imageBank.images;
    std::vector<uint32_t> remap;
    uint64_t bytesUsed = 0;
    for (const auto &image : images)
        bytesUsed += image.rgba.size();
    for (auto &image : prefab.imageBank.images) {
        auto found = std::find_if(images.begin(), images.end(), [&](const UiImage &i) {
            return i.width == image.width && i.height == image.height && i.rgba == image.rgba;
        });
        if (found != images.end())
            remap.push_back(uint32_t(found - images.begin()));
        else {
            bytesUsed += image.rgba.size();
            if (images.size() >= 512 || bytesUsed > MaxImageBytes)
                throw std::runtime_error("Prefab exceeds World image budget");
            remap.push_back(uint32_t(images.size()));
            images.push_back(std::move(image));
        }
    }
    auto rendering=asset_.rendering;
    const auto textureBase=uint32_t(rendering.textures.size()), meshBase=uint32_t(rendering.meshes.size()), shaderBase=uint32_t(rendering.shaders.size()),materialBase=uint32_t(rendering.materials.size());
    for(auto& m:prefab.rendering.materials){if(m.shader!=NoIndex)m.shader+=shaderBase;for(auto& t:m.samplers)t.texture+=textureBase;}
    auto append=[](auto& to,auto& from){to.insert(to.end(),std::make_move_iterator(from.begin()),std::make_move_iterator(from.end()));};
    append(rendering.textures,prefab.rendering.textures);append(rendering.meshes,prefab.rendering.meshes);append(rendering.shaders,prefab.rendering.shaders);append(rendering.materials,prefab.rendering.materials);validateRenderAssets(rendering);
    for (auto &entity : prefab.entities)
        for (auto &value : entity.components) {
            const auto &d = registry_->component(value.type).descriptor;
            for (uint32_t f = 0; f < d.fieldCount; ++f)
                if (d.fields[f].kind == sdk::Kind::Image && value.fields[f] != NoIndex)
                    value.fields[f] = remap.at(value.fields[f]);
                else if(d.fields[f].kind==sdk::Kind::Mesh&&value.fields[f]!=NoIndex)value.fields[f]+=meshBase;
                else if(d.fields[f].kind==sdk::Kind::Material&&value.fields[f]!=NoIndex)value.fields[f]+=materialBase;
        }
    auto id = impl_->nextPrefab++;
    impl_->prefabs.emplace(id, std::move(prefab.entities));
    if (images.size() != asset_.imageBank.images.size()) {
        asset_.imageBank.images = std::move(images);
        impl_->imageCount = asset_.imageBank.images.size();
        ++imageRevision_;
    }
    asset_.rendering=std::move(rendering);impl_->meshCount=asset_.rendering.meshes.size();impl_->materialCount=asset_.rendering.materials.size();
    return id;
}
sdk::Entity SceneRuntime::instantiatePrefab(uint64_t id, sdk::Entity parent) {
    const auto found = impl_->prefabs.find(id);
    if (found == impl_->prefabs.end())
        throw std::runtime_error("Unknown prefab handle");
    const auto &entities = found->second;
    std::vector<std::vector<uint32_t>> children(entities.size());
    size_t commandCount = entities.size();
    for (uint32_t i = 0; i < entities.size(); ++i) {
        if (entities[i].parent != NoIndex)
            children.at(entities[i].parent).push_back(i);
        for (const auto &value : entities[i].components) {
            impl_->access(value.type, true);
            impl_->validate(value.type, value.fields);
            ++commandCount;
        }
    }
    if (commandCount + impl_->commands.size() > 65536 || impl_->slots.size() + entities.size() > 65535)
        throw std::runtime_error("Prefab exceeds World/command capacity");
    const auto base = impl_->commands.size();
    auto root = Impl::create(impl_.get(), parent);
    try {
        std::function<void(uint32_t, sdk::Entity)> copy = [&](uint32_t i, sdk::Entity target) {
            for (const auto &v : entities[i].components)
                impl_->queue({Impl::Op::Set, target, v.type, v.fields});
            for (auto child : children[i])
                copy(child, Impl::create(impl_.get(), target));
        };
        copy(0, root);
        if (!impl_->active) {
            impl_->commit();
            impl_->transforms();
            extract();
            syncUi();
        }
    } catch (...) {
        impl_->commands.resize(base);
        auto it = impl_->slots.find(root);
        if (it != impl_->slots.end())
            impl_->destroyTree(it->second);
        throw;
    }
    return root;
}
void SceneRuntime::unloadPrefab(uint64_t id) {
    if (!impl_->prefabs.erase(id))
        throw std::runtime_error("Unknown prefab handle");
    // Spawned entities own their component values. World images remain while entities may use them.
}
