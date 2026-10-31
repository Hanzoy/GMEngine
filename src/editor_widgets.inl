void icon(Rect box, const char *name, uint32_t tint = 0, float size = 18) {
    const float side = std::min({size * scale_, box.w, box.h});
    icons_->draw(name, {box.x + (box.w - side) * .5f, box.y + (box.h - side) * .5f, side, side},
                 tint ? tint : T::text);
}
void iconButton(Rect box, const char *name, Json command, const std::string &label, bool active = false,
                bool enabled = true, uint32_t tint = 0) {
    enabled = enabled && (!projectBusy() || allowedDuringProjectTask(command.value("op", std::string())));
    const bool hovered = box.contains(mouseX_, mouseY_);
    if (active || hovered && enabled)
        renderer_->rect(box, active ? T::selection : T::hover);
    if (active)
        renderer_->border(box, T::accent, scale_);
    icon(box, name, enabled || active ? (tint ? tint : active ? T::accent : T::text) : T::disabled);
    hits_.push_back({box, std::move(command), label, enabled});
}
void tabButton(Rect box, const std::string &label, Json command, bool active, bool enabled = true) {
    enabled = enabled && (!projectBusy() || allowedDuringProjectTask(command.value("op", std::string())));
    if (active || box.contains(mouseX_, mouseY_) && enabled)
        renderer_->rect(box, active ? T::panel : T::raised);
    centered(box, label, 12, enabled ? (active ? T::text : T::muted) : T::disabled);
    if (active)
        renderer_->rect({box.x + 8 * scale_, box.y + box.h - 2 * scale_, box.w - 16 * scale_, 2 * scale_},
                        T::accent);
    hits_.push_back({box, std::move(command), label, enabled});
}
void checkbox(Rect box, bool checked, Json command, const std::string &label, bool enabled = true) {
    enabled = enabled && (!projectBusy() || allowedDuringProjectTask(command.value("op", std::string())));
    Rect mark{box.x + 4 * scale_, box.y + (box.h - 16 * scale_) * .5f, 16 * scale_, 16 * scale_};
    renderer_->rect(mark, checked ? T::selection : T::field);
    renderer_->border(mark, enabled ? checked ? T::accent : T::border : T::disabled);
    if (checked)
        icon(mark, "check", enabled ? T::text : T::disabled, 14);
    hits_.push_back({box, std::move(command), label, enabled});
}
const char *componentIcon(const std::string &name) const {
    if (name == "Transform3D" || name == "Transform2D" || name == "Pose2D")
        return "move";
    if (name == "MeshRenderer")
        return "cube";
    if (name == "Camera3D" || name == "Camera2D")
        return "camera";
    if (name == "DirectionalLight")
        return "sun";
    if (name == "SpriteRenderer")
        return "image";
    return "component";
}
