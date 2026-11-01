#pragma once
#include "renderer.hpp"
#include "render_assets.hpp"
#include <memory>
namespace gm {
// Lazy GPU resources: a purely 2D scene creates neither programs nor render targets.
class WorldRenderer {
  public:
    explicit WorldRenderer(Renderer &renderer);
    ~WorldRenderer();
    bool begin(const RenderAssets &assets, const RenderFrame &frame, Rect viewport, uint32_t background,
               const ViewCamera3D *camera = nullptr);
    void finish();
    void clear();
    void instancing(bool enabled);
    void temporalAllowed(bool enabled) {
        temporalAllowed_ = enabled;
    }

  private:
    struct Impl;
    Renderer &renderer_;
    std::unique_ptr<Impl> impl_;
    bool instancing_ = true;
    bool temporalAllowed_ = true;
};
} // namespace gm
