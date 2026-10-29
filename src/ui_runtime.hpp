#pragma once
#include "renderer.hpp"
#include "document.hpp"
#include <functional>

namespace gm {
// The runtime owns retained-tree traversal, transforms, clipping and primitive
// widgets. Applications supply native widget extensions and action routing.
struct UiHooks {
    std::function<void(const Json&, float&, float&)> transform;
    std::function<void(const Json&, Rect)> observe;
    std::function<void(const Json&, Rect, float)> paintCustom;
};
class UiRuntime {
public:
    explicit UiRuntime(Renderer& renderer) : renderer_(renderer) {}
    void render(const Json& node, float originX, float originY, float scale, Rect clip, const UiHooks& hooks={});
private:
    Renderer& renderer_;
};
}
