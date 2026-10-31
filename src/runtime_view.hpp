#pragma once
#include "runtime_layout.hpp"
#include "renderer.hpp"
namespace gm {
class RuntimeView {
  public:
    void render(Renderer &renderer, RuntimeDocument &document, float width, float height, float offsetX = 0,
                float offsetY = 0, bool rootBackground = true);
    void clear(Renderer &renderer);
    RuntimeLayout layout;

  private:
    const RuntimeDocument *document_ = nullptr;
    Renderer *renderer_ = nullptr;
    uint64_t assetRevision_ = 0;
    std::vector<ImageHandle> images_;
    uint64_t geometry_ = 0, paintRevision_ = 0, layoutRevision_ = 0;
    uint32_t stableFrames_ = 0;
    std::array<float, 4> viewport_{};
    bool background_ = false;
    void paint(Renderer &renderer, RuntimeDocument &document, float width, float height, float offsetX,
               float offsetY, bool rootBackground);
};
} // namespace gm
