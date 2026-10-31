#include "scene_view.hpp"
#include <cmath>
#include <algorithm>
#include <bx/math.h>
namespace gm {
SceneView::~SceneView() {
    clear();
}
void SceneView::clear() {
    world_.clear();
    for (auto &run : runs_)
        renderer_.releaseGeometry(run.geometry);
    runs_.clear();
    visible_ = {};
    queryVersion_ = UINT64_MAX;
    hudView_.clear(renderer_);
    for (auto handle : images_)
        renderer_.releaseImage(handle);
    images_.clear();
    scene_ = nullptr;
}
void SceneView::render(SceneRuntime &scene, Rect viewport, bool showHud, const ViewCamera3D *editorCamera) {
    if (scene_ != &scene || imageRevision_ != scene.imageRevision()) {
        std::vector<ImageHandle> uploaded;
        try {
            for (const auto &image : scene.asset().imageBank.images)
                uploaded.push_back(renderer_.uploadImage(image));
        } catch (...) {
            for (auto handle : uploaded)
                renderer_.releaseImage(handle);
            throw;
        }
        if (scene_ != &scene)
            clear();
        else {
            world_.clear();
            for (auto handle : images_)
                renderer_.releaseImage(handle);
            for (auto &run : runs_)
                renderer_.releaseGeometry(run.geometry);
            runs_.clear();
        }
        images_ = std::move(uploaded);
        scene_ = &scene;
        imageRevision_ = scene.imageRevision();
    }
    viewport_ = viewport;
    const auto &asset = scene.asset();
    if (editorCamera) {
        // Scene navigation uses the whole viewport, independently of game resolution and HUD.
        if (world_.begin(asset.rendering, scene.rendering(), viewport, asset.background, editorCamera))
            world_.finish();
        renderer_.unclip();
        return;
    }
    float scale = std::min(viewport.w / asset.width, viewport.h / asset.height);
    float x = viewport.x + (viewport.w - asset.width * scale) * .5f,
          y = viewport.y + (viewport.h - asset.height * scale) * .5f;
    renderer_.clip(intersect(viewport, {x, y, asset.width * scale, asset.height * scale}));
    const bool worldLayer=world_.begin(asset.rendering,scene.rendering(),{x,y,asset.width*scale,asset.height*scale},asset.background);
    if(!worldLayer)renderer_.rect({x, y, asset.width * scale, asset.height * scale}, asset.background);
    auto camera = scene.renderCamera();
    const auto &c = camera.transform;
    float determinant = c.a * c.d - c.b * c.c;
    if (std::abs(determinant) < 1e-9f)
        throw std::runtime_error("Camera transform is singular");
    float matrix[16];
    bx::mtxIdentity(matrix);
    float factor = camera.zoom * scale / determinant;
    matrix[0] = c.d * factor;
    matrix[1] = -c.b * factor;
    matrix[4] = -c.c * factor;
    matrix[5] = c.a * factor;
    matrix[12] = x - (c.d * c.x - c.c * c.y) * factor;
    matrix[13] = y - (-c.b * c.x + c.a * c.y) * factor;
    const Rect scissor = intersect(viewport, {x, y, asset.width * scale, asset.height * scale});
    float worldX[4], worldY[4];
    for (int i = 0; i < 4; ++i) {
        float u = (i == 1 || i == 2) ? asset.width / camera.zoom : 0,
              v = i >= 2 ? asset.height / camera.zoom : 0;
        worldX[i] = c.x + c.a * u + c.c * v;
        worldY[i] = c.y + c.b * u + c.d * v;
    }
    auto [left, right] = std::minmax_element(worldX, worldX + 4);
    auto [top, bottom] = std::minmax_element(worldY, worldY + 4);
    Rect area{*left, *top, *right - *left, *bottom - *top};
    if (queryVersion_ != scene.renderVersion() || area.x != queryArea_.x || area.y != queryArea_.y ||
        area.w != queryArea_.w || area.h != queryArea_.h) {
        visible_ = scene.visibleSprites(area);
        queryArea_ = area;
        queryVersion_ = scene.renderVersion();
    }
    const auto &sprites = scene.sprites();
    const auto alpha = float(scene.renderAlpha());
    size_t runIndex = 0;
    auto draw = [&](uint32_t row, bool screen) {
        const auto &sprite = sprites[row];
        auto t = sprite.transform;
        if (screen && sprite.moving) {
            t.x = sprite.previous.x + (t.x - sprite.previous.x) * alpha;
            t.y = sprite.previous.y + (t.y - sprite.previous.y) * alpha;
        }
        std::array<float, 8> points{};
        for (int i = 0; i < 4; ++i) {
            float u = (i == 1 || i == 2) ? sprite.box.w : 0, v = i >= 2 ? sprite.box.h : 0;
            float wx = t.x + t.a * u + t.c * v, wy = t.y + t.b * u + t.d * v;
            points[2 * i] = screen ? matrix[0] * wx + matrix[4] * wy + matrix[12] : wx;
            points[2 * i + 1] = screen ? matrix[1] * wx + matrix[5] * wy + matrix[13] : wy;
        }
        const auto &uv = sprite.region;
        renderer_.sprite(points, sprite.image == NoIndex ? ImageHandle{} : images_.at(sprite.image),
                         sprite.tint, {uv.u0, uv.v0, uv.u1 - uv.u0, uv.v1 - uv.v0});
    };
    uint32_t blocks = 0, lastBlock = UINT32_MAX;
    for (auto row : visible_.rows) {
        auto block = row / SceneRuntime::SpriteBlockSize;
        if (block != lastBlock) {
            ++blocks;
            lastBlock = block;
        }
    }
    // Small scenes stay in the normal batch. Large scenes cache consecutive static runs,
    // preserving painter order around moving objects instead of sorting by texture.
    for (size_t first = 0; first < visible_.rows.size();) {
        auto row = visible_.rows[first];
        if (sprites.size() < 128 || sprites[row].moving || runIndex >= 64) {
            renderer_.clip(scissor);
            draw(row, true);
            ++first;
            continue;
        }
        size_t end = first + 1;
        while (end < visible_.rows.size() && !sprites[visible_.rows[end]].moving)
            ++end;
        std::vector<uint32_t> rows(visible_.rows.begin() + first, visible_.rows.begin() + end);
        std::vector<uint64_t> versions;
        versions.reserve(rows.size());
        for (auto i : rows)
            versions.push_back(scene.spriteBlocks()[i / SceneRuntime::SpriteBlockSize]);
        if (runIndex >= runs_.size())
            runs_.push_back({});
        auto &run = runs_[runIndex++];
        if (run.rows != rows || run.versions != versions || !renderer_.geometryValid(run.geometry)) {
            renderer_.releaseGeometry(run.geometry);
            run.rows = std::move(rows);
            run.versions = std::move(versions);
            renderer_.unclip();
            run.geometry = renderer_.buildGeometry([&] {
                for (auto i : run.rows)
                    draw(i, false);
            });
        }
        if (!renderer_.drawGeometry(run.geometry, matrix, &scissor)) {
            renderer_.clip(scissor);
            for (auto i : run.rows)
                draw(i, true);
        }
        first = end;
    }
    while (runs_.size() > runIndex) {
        renderer_.releaseGeometry(runs_.back().geometry);
        runs_.pop_back();
    }
    renderer_.visibility(uint32_t(sprites.size()), visible_.candidates, uint32_t(visible_.rows.size()),
                         uint32_t(visible_.rows.size()), blocks);
    // Canvas uses design-space screen coordinates, like HUD widgets. Composite it
    // at native output resolution after scene exposure, bloom and antialiasing.
    if(worldLayer)world_.finish();
    const Rect canvasBounds = intersect(viewport, {x, y, asset.width * scale, asset.height * scale});
    renderer_.clip(canvasBounds);
    for (const auto &cmd : scene.canvas()) {
        Rect box{x + cmd.x * scale, y + cmd.y * scale, cmd.w * scale, cmd.h * scale};
        switch (cmd.kind) {
        case sdk::CanvasKind::Rect:
            renderer_.rect(box, cmd.color);
            break;
        case sdk::CanvasKind::Border:
            renderer_.border(box, cmd.color, cmd.size * scale);
            break;
        case sdk::CanvasKind::Text:
            renderer_.text(box.x, box.y, cmd.text, cmd.size * scale, cmd.color, box.w);
            break;
        case sdk::CanvasKind::Clip:
            renderer_.clip(intersect(canvasBounds, box));
            break;
        case sdk::CanvasKind::ResetClip:
            renderer_.clip(canvasBounds);
            break;
        case sdk::CanvasKind::Line: {
            float dx = cmd.w - cmd.x, dy = cmd.h - cmd.y, length = std::hypot(dx, dy);
            if (length < .0001f)
                break;
            float nx = -dy / length * cmd.size * scale * .5f, ny = dx / length * cmd.size * scale * .5f;
            float x2 = x + cmd.w * scale, y2 = y + cmd.h * scale;
            renderer_.sprite(
                {box.x + nx, box.y + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, box.x - nx, box.y - ny}, {},
                cmd.color);
            break;
        }
        }
    }
    renderer_.clip(canvasBounds);
    if (showHud)
        hudView_.render(renderer_, scene.hud(), viewport.w, viewport.h, viewport.x, viewport.y, false);
    renderer_.unclip();
}
uint32_t SceneView::hitUi(const SceneRuntime &scene, float x, float y) const {
    if (scene_ != &scene || !viewport_.contains(x, y))
        return NoIndex;
    x -= viewport_.x;
    y -= viewport_.y;
    for (auto it = scene.asset().bindings.rbegin(); it != scene.asset().bindings.rend(); ++it)
        if (it->kind == BindingKind::NumberChange && it->node < hudView_.layout.clips().size() &&
            hudView_.layout.clips()[it->node].contains(x, y))
            return it->node;
    return hudView_.layout.hit(x, y);
}
} // namespace gm
