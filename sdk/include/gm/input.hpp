#pragma once
#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace gm::sdk {
enum class InputSource { Key, MouseButton, PadButton, PadAxis, TouchRegion };
struct ActionBinding {
    TypeId action = 0, context = 0;
    InputSource source = InputSource::Key;
    uint32_t code = 0, device = 0;
    float scale = 1, deadzone = .15f;
    float x = 0, y = 0, w = 0, h = 0;
};
struct ActionValue {
    float value = 0;
    bool held = false, pressed = false, released = false;
};
// Game-owned configuration; vectors never cross the native ABI. Bind by stable action
// names, not physical keys in Systems. A modal context suppresses every other context.
class ActionMap {
    struct Action {
        TypeId id;
        ActionValue value;
    };
    std::vector<ActionBinding> bindings_;
    std::vector<Action> actions_;
    std::vector<TypeId> disabled_;
    TypeId modal_ = 0;

  public:
    void bind(ActionBinding b) {
        if (!b.action || uint32_t(b.source) > uint32_t(InputSource::TouchRegion) || bindings_.size() >= 512 ||
            !std::isfinite(b.scale) || std::abs(b.scale) > 1 || !std::isfinite(b.deadzone) ||
            b.deadzone < 0 || b.deadzone >= 1 || b.device >= 4 ||
            (b.source == InputSource::Key && b.code >= 256) ||
            (b.source == InputSource::PadAxis && b.code >= 6) ||
            (b.source == InputSource::TouchRegion &&
             (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w) || !std::isfinite(b.h) ||
              b.w <= 0 || b.h <= 0)))
            throw std::runtime_error("Invalid action binding");
        bindings_.push_back(b);
        if (std::none_of(actions_.begin(), actions_.end(), [&](auto a) { return a.id == b.action; }))
            actions_.push_back({b.action, {}});
    }
    void unbind(TypeId action) {
        std::erase_if(bindings_, [&](auto b) { return b.action == action; });
    }
    void enable(TypeId context, bool enabled) {
        std::erase(disabled_, context);
        if (!enabled)
            disabled_.push_back(context);
    }
    void modal(TypeId context = 0) {
        modal_ = context;
    }
    void update(const Input &input) {
        for (auto &action : actions_) {
            float value = 0;
            bool edge = false;
            if (input.focused)
                for (const auto &b : bindings_)
                    if (b.action == action.id && (!modal_ || modal_ == b.context) &&
                        std::find(disabled_.begin(), disabled_.end(), b.context) == disabled_.end()) {
                        float v = 0;
                        const auto &pad = input.gamepads[b.device];
                        switch (b.source) {
                        case InputSource::Key:
                            v = input.held(b.code) ? 1.f : 0.f;
                            edge |= input.hit(b.code);
                            break;
                        case InputSource::MouseButton:
                            v = (input.buttons & b.code) ? 1.f : 0.f;
                            edge |= (input.pressed & b.code) != 0;
                            break;
                        case InputSource::PadButton:
                            v = pad.connected && (pad.buttons & b.code) ? 1.f : 0.f;
                            break;
                        case InputSource::PadAxis:
                            if (pad.connected && std::isfinite(pad.axes[b.code])) {
                                float raw = std::clamp(pad.axes[b.code], -1.f, 1.f);
                                v = std::copysign(
                                    std::max(0.f, (std::abs(raw) - b.deadzone) / (1 - b.deadzone)), raw);
                            }
                            break;
                        case InputSource::TouchRegion:
                            for (uint32_t i = 0; i < std::min(input.touchCount, 10u); ++i) {
                                const auto &t = input.touches[i];
                                if (t.x >= b.x && t.y >= b.y && t.x < b.x + b.w && t.y < b.y + b.h) {
                                    if (t.down)
                                        v = 1;
                                    edge |= t.pressed != 0;
                                }
                            }
                            break;
                        }
                        value += v * b.scale;
                    }
            auto old = action.value.held;
            value = std::clamp(value, -1.f, 1.f);
            bool held = std::abs(value) > .0001f;
            action.value = {value, held, (!old && held) || edge, (old && !held) || (!held && edge)};
        }
    }
    ActionValue get(TypeId id) const {
        for (auto &a : actions_)
            if (a.id == id)
                return a.value;
        return {};
    }
};
// Platform adapter with stable touch IDs, retained edges, and independent fingers.
class TouchInput {
    Touch values_[10]{};

  public:
    void clear() {
        for (auto &t : values_)
            t = {};
    }
    void set(int32_t id, float x, float y, bool down) {
        if (id < 0 || !std::isfinite(x) || !std::isfinite(y))
            return;
        Touch *target = nullptr;
        for (auto &t : values_)
            if (t.id == id) {
                target = &t;
                break;
            }
        if (!target && down)
            for (auto &t : values_)
                if (t.id < 0) {
                    target = &t;
                    target->id = id;
                    break;
                }
        if (!target)
            return;
        target->x = x;
        target->y = y;
        target->pressed |= down && !target->down;
        target->released |= !down && target->down;
        target->down = down;
    }
    void sample(Input &out) {
        out.touchCount = 0;
        for (auto &t : values_)
            if (t.id >= 0) {
                out.touches[out.touchCount++] = t;
                t.pressed = t.released = 0;
                if (!t.down)
                    t = {};
            }
    }
};
} // namespace gm::sdk
