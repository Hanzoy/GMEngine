#pragma once
#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>

namespace gm::sdk {
// Shared input routing for custom Canvas UIs. Register contexts before begin(), topmost first
// at hit-test time. Modal roots exclude siblings, and captured pointers stay with their owner.
class UiInputRouter {
  public:
    struct Region {
        float x = 0, y = 0, w = 0, h = 0;
        bool hit(float px, float py) const {
            return px >= x && py >= y && px < x + w && py < y + h;
        }
    };
    struct Layer {
        TypeId id = 0, parent = 0;
        Region bounds;
        bool pointer = true, keyboard = true, enabled = true;
    };
    void define(Layer layer) {
        if (!layer.id || layer.parent == layer.id)
            throw std::invalid_argument("Invalid UI input context");
        if (!std::isfinite(layer.bounds.x) || !std::isfinite(layer.bounds.y) ||
            !std::isfinite(layer.bounds.w) || !std::isfinite(layer.bounds.h) || layer.bounds.w < 0 ||
            layer.bounds.h < 0)
            throw std::invalid_argument("Invalid UI input bounds");
        if (layer.parent && !find(layer.parent))
            throw std::invalid_argument("Define UI input parent first");
        if (find(layer.id))
            throw std::invalid_argument("Duplicate UI input context");
        if (layers_.size() >= 256)
            throw std::runtime_error("Too many UI input contexts");
        layers_.push_back(layer);
    }
    void clearLayers() {
        layers_.clear();
        capture_ = focus_ = modal_ = pointer_ = keyboard_ = 0;
    }
    void enable(TypeId id, bool enabled) {
        auto it =
            std::find_if(layers_.begin(), layers_.end(), [&](const Layer &layer) { return layer.id == id; });
        if (it == layers_.end())
            throw std::invalid_argument("Unknown UI input context");
        it->enabled = enabled;
        if (capture_ && !eligible(capture_))
            capture_ = 0;
        route();
    }
    void modal(TypeId root) {
        if (root && !find(root))
            throw std::invalid_argument("Unknown modal root");
        modal_ = root;
        if (capture_ && !eligible(capture_))
            capture_ = 0;
        if (focus_ && !eligible(focus_))
            focus_ = 0;
        route();
    }
    void focus(TypeId id) {
        if (id && (!find(id) || !find(id)->keyboard || !eligible(id)))
            throw std::invalid_argument("Invalid UI keyboard focus");
        focus_ = id;
        route();
    }
    void capture(TypeId id) {
        if (!find(id) || !eligible(id) || pointer_ != id)
            throw std::invalid_argument("Only the pointer owner can capture");
        capture_ = id;
        route();
    }
    void release(TypeId id) {
        if (capture_ == id)
            capture_ = 0;
        route();
    }
    void begin(Input input) {
        if (!(input.buttons | input.pressed | input.released))
            capture_ = 0;
        if (!input.focused) {
            input = {};
            capture_ = focus_ = 0;
        }
        input_ = input;
        pointerConsumed_ = keyboardConsumed_ = false;
        route();
    }
    Input sample(TypeId id) const {
        if (!id || !find(id) || !eligible(id))
            return {};
        Input out = input_;
        if (id != pointer_ || pointerConsumed_) {
            out.mouseX = out.mouseY = -1;
            out.buttons = out.pressed = out.released = 0;
            out.wheel = 0;
        }
        if (id != keyboard_ || keyboardConsumed_) {
            for (auto &k : out.keys)
                k = 0;
            for (auto &k : out.keyPress)
                k = 0;
        }
        return out;
    }
    void consumePointer(TypeId id) {
        if (id == pointer_)
            pointerConsumed_ = true;
    }
    void consumeKeyboard(TypeId id) {
        if (id == keyboard_)
            keyboardConsumed_ = true;
    }
    TypeId pointerOwner() const {
        return pointer_;
    }
    TypeId keyboardOwner() const {
        return keyboard_;
    }

  private:
    const Layer *find(TypeId id) const {
        for (const auto &layer : layers_)
            if (layer.id == id)
                return &layer;
        return nullptr;
    }
    bool eligible(TypeId id) const {
        bool accepted = !modal_;
        for (size_t depth = 0; id && depth <= layers_.size(); ++depth) {
            auto layer = find(id);
            if (!layer || !layer->enabled)
                return false;
            if (id == modal_)
                accepted = true;
            id = layer->parent;
        }
        return accepted;
    }
    void route() {
        pointer_ = keyboard_ = 0;
        if (!input_.focused)
            return;
        if (capture_ && eligible(capture_))
            pointer_ = capture_;
        else
            for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
                if (it->pointer && eligible(it->id) && it->bounds.hit(input_.mouseX, input_.mouseY)) {
                    pointer_ = it->id;
                    break;
                }
        if (focus_ && eligible(focus_))
            keyboard_ = focus_;
        else
            for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
                if (it->keyboard && eligible(it->id)) {
                    keyboard_ = it->id;
                    break;
                }
    }
    std::vector<Layer> layers_;
    Input input_{};
    TypeId modal_ = 0, capture_ = 0, focus_ = 0, pointer_ = 0, keyboard_ = 0;
    bool pointerConsumed_ = false, keyboardConsumed_ = false;
};
} // namespace gm::sdk
