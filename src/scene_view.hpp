#pragma once
#include "scene.hpp"
#include "runtime_view.hpp"
namespace gm {
class SceneView {
  public:
    explicit SceneView(Renderer &renderer) : renderer_(renderer) {}
    ~SceneView();
    void render(SceneRuntime &scene, Rect viewport, bool showHud = true);
    uint32_t hitUi(const SceneRuntime &scene, float x, float y) const;
    void clear();

  private:
    Renderer &renderer_;
    const SceneRuntime *scene_ = nullptr;
    Rect viewport_{};
    RuntimeView hudView_;
    std::vector<ImageHandle> images_;
    uint64_t imageRevision_ = 0;
    struct Run {
        std::vector<uint32_t> rows;
        std::vector<uint64_t> versions;
        uint64_t geometry = 0;
    };
    std::vector<Run> runs_;
    SpatialGrid::Query visible_;
    Rect queryArea_{};
    uint64_t queryVersion_ = UINT64_MAX;
};
} // namespace gm
