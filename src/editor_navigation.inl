// Editor camera state belongs to the workspace, never to Document / ECS / undo.
bool scene3D() const {
    if (!sceneMode())
        return false;
    for (const auto &e : doc_.data()["entities"])
        if (gm::findComponent(e, "Transform3D") || gm::findComponent(e, "Camera3D") ||
            gm::findComponent(e, "MeshRenderer"))
            return true;
    return false;
}
bool sceneNavigable() const {
    return sceneMode() && (!scenePlay_ || !sceneGameView_);
}
void ensureEdit3D() {
    if (scenePlay_ || !scene3D())
        return;
    if (edit3DRevision_ == doc_.revision() && edit3DPath_ == doc_.path())
        return;
    edit3DView_.reset();
    edit3D_.reset();
    edit3DRevision_ = doc_.revision();
    edit3DPath_ = doc_.path();
    try {
        auto asset = gm::compileScene(doc_.data(), doc_.path().parent_path());
        asset.disabledSystems.clear();
        for (const auto &system : gm::gameRegistry()->systems())
            asset.disabledSystems.push_back(system.descriptor.name);
        edit3D_ = std::make_unique<gm::SceneRuntime>(std::move(asset));
        edit3DView_ = std::make_unique<gm::SceneView>(*renderer_);
    } catch (const std::exception &e) {
        fail(e.what());
    }
}
gm::SceneRuntime *navigationScene() {
    ensureEdit3D();
    return scenePlay_ ? scenePlay_.get() : edit3D_.get();
}
gm::EditorBounds3D navigationBounds(gm::SceneRuntime &scene, const std::string &id) {
    gm::EditorBounds3D bounds;
    const auto selected = id.empty() ? gm::NoIndex : scene.find(id);
    if (!id.empty() && selected == gm::NoIndex)
        return bounds;
    auto included = [&](uint32_t index) {
        if (id.empty())
            return true;
        for (size_t depth = 0; index != gm::NoIndex && depth < scene.entities().size(); ++depth) {
            if (index == selected)
                return true;
            index = scene.entities()[index].parent;
        }
        return false;
    };
    for (const auto &draw : scene.rendering().meshes)
        if (included(draw.entity))
            bounds.include(scene.asset().rendering.meshes.at(draw.mesh), draw.transform);
    if (!bounds.valid && selected != gm::NoIndex) {
        // Empty entities / cameras can still be framed; account for parent transforms.
        std::vector<uint32_t> chain;
        for (auto i = selected; i != gm::NoIndex && chain.size() < scene.entities().size();
             i = scene.entities()[i].parent)
            chain.push_back(i);
        auto world = gm::identity3D();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!gm::findValue(scene.entities()[*it], gm::sdk::id("Transform3D")))
                continue;
            auto value = scene.component(*it, gm::sdk::id("Transform3D"));
            if (value.size() == sizeof(gm::sdk::Transform3D) / 4) {
                gm::sdk::Transform3D t;
                std::memcpy(&t, value.data(), sizeof(t));
                world = gm::multiply3D(world, gm::local3D(t));
            }
        }
        bounds.include(gm::transformPoint(world, {0, 0, 0}));
    }
    return bounds;
}
gm::EditorCamera &sceneCamera() {
    auto key = pathText(doc_.path());
    auto found = sceneCameras_.find(key);
    if (found != sceneCameras_.end())
        return found->second;
    gm::EditorCamera camera;
    if (auto *scene = navigationScene()) {
        if (scene->rendering().cameraEnabled)
            camera.fromGameCamera(scene->rendering().camera);
        else
            camera.frame(navigationBounds(*scene, ""), canvasArea_.w / std::max(1.f, canvasArea_.h));
    }
    return sceneCameras_.emplace(key, camera).first->second;
}
Json cameraJson(const gm::EditorCamera &camera) const {
    return {{"position", camera.position},
            {"yaw", camera.yaw},
            {"pitch", camera.pitch},
            {"distance", camera.distance},
            {"speed", camera.speed}};
}
gm::EditorCamera readCamera(const Json &j, gm::EditorCamera camera = {}) const {
    if (j.contains("position")) {
        if (!j["position"].is_array() || j["position"].size() != 3)
            throw std::runtime_error("position requires three numbers");
        camera.position = j["position"].get<gm::Vec3>();
    }
    camera.yaw = j.value("yaw", camera.yaw);
    camera.pitch = j.value("pitch", camera.pitch);
    camera.distance = j.value("distance", camera.distance);
    camera.speed = j.value("speed", camera.speed);
    if (!camera.valid())
        throw std::runtime_error("Invalid scene camera: finite position, yaw -360..360, pitch -89.5..89.5, "
                                 "distance .02..100000, speed .01..10000 required");
    return camera;
}
void frameSelection(bool all = false) {
    if (sceneNavigable() && scene3D()) {
        if (auto *scene = navigationScene()) {
            auto bounds = navigationBounds(*scene, all ? std::string() : doc_.selected());
            if (!bounds.valid)
                bounds = navigationBounds(*scene, "");
            sceneCamera().frame(bounds, canvasArea_.w / std::max(1.f, canvasArea_.h));
        }
    } else if (!scenePlay_) {
        // 2D/UI framing uses un-clipped authoring bounds, including off-screen nodes.
        if (!all)
            for (const auto &node : nodes_)
                if (node.id == doc_.selected() && node.rect.w > 0 && node.rect.h > 0) {
                    float factor = std::min(canvasArea_.w / std::max(1.f, node.rect.w + 80 * scale_),
                                            canvasArea_.h / std::max(1.f, node.rect.h + 80 * scale_));
                    const float cx = canvasArea_.x + canvasArea_.w * .5f,
                                cy = canvasArea_.y + canvasArea_.h * .5f;
                    float nx = node.rect.x + node.rect.w * .5f, ny = node.rect.y + node.rect.h * .5f;
                    zoomAt(nx, ny, factor);
                    panX_ += cx - nx;
                    panY_ += cy - ny;
                    requestFrame();
                    return;
                }
        zoom_ = 1;
        panX_ = panY_ = 0;
    }
    requestFrame();
}
Json navigationCommand(const Json &request) {
    if (!sceneNavigable() || !scene3D())
        throw std::runtime_error("Open a 3D Scene view to use its editor camera");
    auto &camera = sceneCamera();
    const auto op = request.at("op").get<std::string>();
    if (op == "view.camera") {
        if (request.contains("set"))
            camera = readCamera(request.at("set"), camera);
    } else if (op == "view.pick") {
        float x = request.at("x").get<float>(), y = request.at("y").get<float>();
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
            throw std::runtime_error("Pick x/y must be normalized 0..1 viewport coordinates");
        if (auto *scene = navigationScene()) {
            auto id = gm::pickEditorMesh(scene->asset().rendering, scene->rendering(), camera, x, y,
                                         canvasArea_.w / std::max(1.f, canvasArea_.h));
            if (id != gm::NoIndex && gm::findEntity(doc_.data(), scene->entities()[id].id))
                command({{"op", "select"}, {"id", scene->entities()[id].id}});
        }
    } else if (op == "view.navigate") {
        auto action = request.at("action").get<std::string>();
        auto next = camera;
        auto number = [&](const char *key) {
            float v = request.value(key, 0.f);
            if (!std::isfinite(v) || std::abs(v) > 10000)
                throw std::runtime_error("Navigation values must be finite, within +/-10000");
            return v;
        };
        if (action == "look" || action == "orbit")
            next.look(number("x"), number("y"), action == "orbit");
        else if (action == "pan")
            next.pan(number("x"), number("y"), canvasArea_.h);
        else if (action == "dolly")
            next.dolly(number("steps"));
        else if (action == "speed")
            next.changeSpeed(number("steps"));
        else if (action == "fly")
            next.fly(number("x"), number("y"), number("z"), number("seconds"), request.value("fast", false));
        else
            throw std::runtime_error("Unknown navigation action");
        if (!next.valid())
            throw std::runtime_error("Navigation exceeds scene camera limits");
        camera = next;
    }
    auto result = cameraJson(camera);
    result["viewport"] = {canvasArea_.x, canvasArea_.y, canvasArea_.w, canvasArea_.h};
    result["gesture"] = navigationMode_;
    result["focused"] = GetForegroundWindow() == window_;
    result["pointer"] = {mouseX_, mouseY_};
    result["selected"] = doc_.selected();
    result["gameView"] = sceneGameView_;
    result["revision"] = doc_.revision();
    result["dirty"] = doc_.dirty();
    return {{"ok", true}, {"result", std::move(result)}};
}
bool beginNavigation(float x, float y, int button) {
    if (!sceneNavigable() || !canvasArea_.contains(x, y))
        return false;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (button == 1 && !alt && tool_ != "hand")
        return false;
    if (!commit())
        return true;
    if (!navigationMode_.empty())
        return true;
    navigationButton_ = button;
    // Navigation keys belong to the viewport, even when a CJK IME is active.
    ImmAssociateContextEx(window_, nullptr, 0);
    startX_ = x;
    startY_ = y;
    mouseX_ = x;
    mouseY_ = y;
    if (!scene3D()) {
        dragMode_ = "pan";
        oldPanX_ = panX_;
        oldPanY_ = panY_;
        navigationMode_ = "pan2D";
        SetCapture(window_);
        return true;
    }
    sceneCamera();
    navigationMode_ = button == 2 ? (alt ? "dolly" : "fly") : (button == 4 || ctrl || !alt) ? "pan" : "orbit";
    SetCapture(window_);
    GetCursorPos(&navigationCursor_);
    POINT a{LONG(canvasArea_.x), LONG(canvasArea_.y)},
        b{LONG(canvasArea_.x + canvasArea_.w), LONG(canvasArea_.y + canvasArea_.h)};
    ClientToScreen(window_, &a);
    ClientToScreen(window_, &b);
    RECT clip{a.x, a.y, b.x, b.y};
    ClipCursor(&clip);
    RAWINPUTDEVICE mouse{1, 2, 0, window_};
    navigationRaw_ = RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;
    if (navigationRaw_) {
        ShowCursor(FALSE);
        navigationHidden_ = true;
    }
    navigationTick_ = std::chrono::steady_clock::now();
    requestFrame();
    return true;
}
void endNavigation() {
    if (navigationMode_.empty())
        return;
    navigationMode_.clear();
    updateTextInput();
    navigationButton_ = 0;
    dragMode_.clear();
    if (navigationRaw_) {
        RAWINPUTDEVICE mouse{1, 2, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
        navigationRaw_ = false;
    }
    if (navigationHidden_) {
        ClipCursor(nullptr);
        SetCursorPos(navigationCursor_.x, navigationCursor_.y);
        ShowCursor(TRUE);
        navigationHidden_ = false;
    } else
        ClipCursor(nullptr);
    if (GetCapture() == window_)
        ReleaseCapture();
    requestFrame();
}
void navigationDelta(float dx, float dy) {
    if (navigationMode_.empty() || navigationMode_ == "pan2D")
        return;
    auto &camera = sceneCamera();
    auto before = camera;
    if (navigationMode_ == "fly" || navigationMode_ == "orbit")
        camera.look(dx / scale_, dy / scale_, navigationMode_ == "orbit");
    else if (navigationMode_ == "pan")
        camera.pan(dx, dy, canvasArea_.h);
    else if (navigationMode_ == "dolly")
        camera.dolly((dx - dy) / scale_ * .05f);
    if (!camera.valid())
        camera = before;
    requestFrame();
}
void updateNavigation() {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - navigationTick_).count();
    navigationTick_ = now;
    if (navigationMode_ != "fly")
        return;
    if (GetForegroundWindow() != window_ || IsIconic(window_)) {
        endNavigation();
        return;
    }
    auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    float x = float(down('D') || down(VK_RIGHT)) - float(down('A') || down(VK_LEFT)),
          y = float(down('E') || down(VK_PRIOR)) - float(down('Q') || down(VK_NEXT)),
          z = float(down('W') || down(VK_UP)) - float(down('S') || down(VK_DOWN));
    if (x || y || z) {
        auto &camera = sceneCamera();
        auto before = camera;
        camera.fly(x, y, z, dt, down(VK_SHIFT));
        if (!camera.valid())
            camera = before;
        requestFrame();
    }
}
void drawSceneSelection() {
    if (assetInspection_ || doc_.selected().empty())
        return;
    auto *scene = navigationScene();
    if (!scene)
        return;
    auto bounds = navigationBounds(*scene, doc_.selected());
    if (!bounds.valid)
        return;
    auto &camera = sceneCamera();
    auto view = gm::inverseAffine3D(camera.matrix());
    gm::Vec3 points[8];
    for (int i = 0; i < 8; ++i)
        points[i] = gm::transformPoint(view, {(i & 1) ? bounds.maximum[0] : bounds.minimum[0],
                                              (i & 2) ? bounds.maximum[1] : bounds.minimum[1],
                                              (i & 4) ? bounds.maximum[2] : bounds.minimum[2]});
    renderer_->clip(canvasArea_);
    const float factor = canvasArea_.h * .5f / std::tan(30 * gm::EditorCamera::radians);
    for (int i = 0; i < 8; ++i)
        for (int axis = 1; axis <= 4; axis *= 2)
            if (!(i & axis)) {
                auto a = points[i], b = points[i | axis];
                constexpr float nearZ = .01f;
                if (a[2] < nearZ && b[2] < nearZ)
                    continue;
                if (a[2] < nearZ) {
                    float t = (nearZ - a[2]) / (b[2] - a[2]);
                    a = gm::add3D(a, gm::add3D(b, a, -1), t);
                }
                if (b[2] < nearZ) {
                    float t = (nearZ - b[2]) / (a[2] - b[2]);
                    b = gm::add3D(b, gm::add3D(a, b, -1), t);
                }
                float ax = canvasArea_.x + canvasArea_.w * .5f + a[0] / a[2] * factor,
                      ay = canvasArea_.y + canvasArea_.h * .5f - a[1] / a[2] * factor,
                      bx = canvasArea_.x + canvasArea_.w * .5f + b[0] / b[2] * factor,
                      by = canvasArea_.y + canvasArea_.h * .5f - b[1] / b[2] * factor;
                float length = std::hypot(bx - ax, by - ay);
                if (length < .01f)
                    continue;
                float nx = -(by - ay) / length * scale_, ny = (bx - ax) / length * scale_;
                renderer_->sprite({ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny},
                                  {}, C("#F3B65F"));
            }
}
