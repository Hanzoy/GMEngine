#pragma once
#include "scene.hpp"
#include "runtime_view.hpp"
namespace gm {
class SceneView {
public:
    explicit SceneView(Renderer& renderer):renderer_(renderer){}
    ~SceneView();
    void render(SceneRuntime& scene,Rect viewport,bool showHud=true);
    uint32_t hitUi(const SceneRuntime& scene,float x,float y) const;
    void clear();
private:
    Renderer& renderer_;const SceneRuntime* scene_=nullptr;Rect viewport_{};
    RuntimeView hudView_;std::vector<bgfx::TextureHandle> images_;
};
}
