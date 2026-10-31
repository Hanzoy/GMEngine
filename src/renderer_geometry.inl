// Persistent geometry is scoped to this Renderer and retains original painter order.
bool Renderer::geometryValid(uint64_t id) const {
    auto it = geometry_.find(id);
    return it != geometry_.end() && it->second.epoch == textureEpoch_;
}
void Renderer::releaseGeometry(uint64_t id) {
    auto it = geometry_.find(id);
    if (it == geometry_.end())
        return;
    for (auto &b : it->second.batches) {
        bgfx::destroy(b.vb);
        bgfx::destroy(b.ib);
        geometryBytes_ -= b.bytes;
    }
    geometry_.erase(it);
}
uint64_t Renderer::buildGeometry(const std::function<void()> &build) {
    if (recording_)
        throw std::runtime_error("Nested geometry recording");
    flush();
    for (auto it = geometry_.begin(); it != geometry_.end();) {
        auto id = it->first;
        if (it->second.epoch != textureEpoch_ || frame_ - it->second.lastUsed > 600) {
            ++it;
            releaseGeometry(id);
        } else
            ++it;
    }
    const uint64_t id = nextGeometry_++;
    geometry_.emplace(id, Geometry{{}, textureEpoch_, frame_});
    recording_ = id;
    captureOverflow_ = false;
    try {
        build();
        flush();
        recording_ = 0;
    } catch (...) {
        recording_ = 0;
        vertices_.clear();
        indices_.clear();
        batchQuads_ = 0;
        releaseGeometry(id);
        throw;
    }
    if (captureOverflow_ || !geometryValid(id)) {
        releaseGeometry(id);
        return 0;
    }
    ++stats_.geometryBuilds;
    return id;
}
void Renderer::submit(bgfx::TextureHandle texture, const float *transform, const Rect *scissor) {
    float identity[16];
    if (!transform) {
        bx::mtxIdentity(identity);
        transform = identity;
    }
    bgfx::setTransform(transform);
    const float output[] = {linearOutput_?1.f:0.f, 1, 0, 0};
    bgfx::setUniform(output_, output);
    bgfx::setTexture(0, sampler_, texture);
    if (scissor) {
        auto clip=intersect(*scissor,layerBounds_);float sx=layerWidth_/layerBounds_.w,sy=layerHeight_/layerBounds_.h;
        auto x=std::clamp(int(std::floor((clip.x-layerBounds_.x)*sx)),0,int(layerWidth_)),y=std::clamp(int(std::floor((clip.y-layerBounds_.y)*sy)),0,int(layerHeight_));
        auto right=std::clamp(int(std::ceil((clip.x+clip.w-layerBounds_.x)*sx)),x,int(layerWidth_)),bottom=std::clamp(int(std::ceil((clip.y+clip.h-layerBounds_.y)*sy)),y,int(layerHeight_));
        bgfx::setScissor(uint16_t(x), uint16_t(y), uint16_t(right - x), uint16_t(bottom - y));
    } else
        bgfx::setScissor(UINT16_MAX);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(activeView_, program_);
    ++stats_.drawCalls;
}
bool Renderer::drawGeometry(uint64_t id, const float *transform, const Rect *scissor) {
    if (!geometryValid(id))
        return false;
    flush();
    auto &geometry = geometry_.at(id);
    geometry.lastUsed = frame_;
    for (const auto &b : geometry.batches) {
        bgfx::setVertexBuffer(0, b.vb);
        bgfx::setIndexBuffer(b.ib);
        submit(b.texture, transform, scissor);
        ++stats_.cachedDraws;
        stats_.quads += b.quads;
        for (auto &page : textPages_)
            if (page.handle.idx == b.texture.idx)
                page.lastUsed = frame_;
    }
    return true;
}
void Renderer::flush() {
    if (vertices_.empty())
        return;
    const auto vcount = uint32_t(vertices_.size()), icount = uint32_t(indices_.size());
    const uint64_t bytes = uint64_t(vcount) * sizeof(Vertex) + uint64_t(icount) * sizeof(uint16_t);
    if (recording_) {
        if (geometryBytes_ + bytes > 32ull * 1024 * 1024)
            captureOverflow_ = true;
        if (!captureOverflow_) {
            auto vb =
                bgfx::createVertexBuffer(bgfx::copy(vertices_.data(), vcount * sizeof(Vertex)), layout_);
            auto ib = bgfx::createIndexBuffer(bgfx::copy(indices_.data(), icount * sizeof(uint16_t)));
            if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
                if (bgfx::isValid(vb))
                    bgfx::destroy(vb);
                if (bgfx::isValid(ib))
                    bgfx::destroy(ib);
                captureOverflow_ = true;
            } else {
                try {
                    geometry_.at(recording_).batches.push_back({vb, ib, batchTexture_, batchQuads_, bytes});
                } catch (...) {
                    bgfx::destroy(vb);
                    bgfx::destroy(ib);
                    throw;
                }
                geometryBytes_ += bytes;
                stats_.vertexUploadBytes += bytes;
            }
        }
    } else {
        if (bgfx::getAvailTransientVertexBuffer(vcount, layout_) < vcount ||
            bgfx::getAvailTransientIndexBuffer(icount) < icount)
            throw std::runtime_error("Transient geometry capacity exceeded");
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        bgfx::allocTransientVertexBuffer(&vb, vcount, layout_);
        bgfx::allocTransientIndexBuffer(&ib, icount);
        std::memcpy(vb.data, vertices_.data(), vcount * sizeof(Vertex));
        std::memcpy(ib.data, indices_.data(), icount * sizeof(uint16_t));
        bgfx::setVertexBuffer(0, &vb);
        bgfx::setIndexBuffer(&ib);
        submit(batchTexture_);
        stats_.quads += batchQuads_;
        stats_.vertexUploadBytes += bytes;
    }
    vertices_.clear();
    indices_.clear();
    batchQuads_ = 0;
}
void Renderer::visibility(uint32_t total, uint32_t candidates, uint32_t visible, uint32_t submitted,
                          uint32_t blocks) {
    stats_.totalSprites += total;
    stats_.spriteCandidates += candidates;
    stats_.visibleSprites += visible;
    stats_.submittedSprites += submitted;
    stats_.visibleBlocks += blocks;
}
