#pragma once
#include "renderer.hpp"
#include <string_view>
namespace gm {
// SVG sources are embedded at build time. Rasterize once per physical icon size,
// then reuse the renderer's atlas; tinting a hover/disabled icon never uploads it again.
std::vector<std::string_view> editorIconNames();
UiImage rasterEditorIcon(std::string_view name, int pixels);
class EditorIcons {
  public:
    explicit EditorIcons(Renderer &renderer) : renderer_(renderer) {}
    EditorIcons(const EditorIcons &) = delete;
    EditorIcons &operator=(const EditorIcons &) = delete;
    ~EditorIcons() {
        clear();
    }
    void draw(std::string_view name, Rect box, uint32_t tint);
    void clear();

  private:
    Renderer &renderer_;
    std::map<std::pair<std::string, int>, ImageHandle> cache_;
};
} // namespace gm
