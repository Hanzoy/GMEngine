// Included inside namespace gm. Logical images can share a physical atlas page.
ImageHandle Renderer::uploadImage(const UiImage &image) {
    if (!image.width || !image.height || image.width > MaxImageDimension ||
        image.height > MaxImageDimension || image.rgba.size() != uint64_t(image.width) * image.height * 4)
        throw std::runtime_error("Invalid image upload");
    auto hash = contentHash(image.rgba);
    auto key = std::make_pair(hash, std::make_pair(image.width, image.height));
    if (auto it = imageKeys_.find(key); it != imageKeys_.end()) {
        auto &entry = images_.at(it->second);
        ++entry.references;
        entry.lastUsed = frame_;
        ++stats_.imageCacheHits;
        return {it->second};
    }
    if (images_.size() >= 4096)
        trimImages(true);
    if (images_.size() >= 4096)
        throw std::runtime_error("Renderer image handle budget exceeded");
    constexpr uint32_t side = 512;
    constexpr uint64_t limit = 192ull * 1024 * 1024;
    Texture texture{BGFX_INVALID_HANDLE, int(image.width), int(image.height), frame_};
    // One-pixel edge extrusion prevents linear filtering from bleeding between regions.
    if (image.width <= 254 && image.height <= 254) {
        ImagePage *chosen = nullptr;
        for (auto &page : imagePages_) {
            int x = page.x, y = page.y;
            if (x + int(image.width) + 2 > int(side)) {
                x = 0;
                y += page.rowHeight;
            }
            if (y + image.height + 2 <= side) {
                if (x == 0 && page.x) {
                    page.x = 0;
                    page.y = y;
                    page.rowHeight = 0;
                }
                chosen = &page;
                break;
            }
        }
        if (!chosen && imagePages_.size() < 32) {
            if (imageBytes_ + side * side * 4 > limit)
                trimImages(true);
            if (imageBytes_ + side * side * 4 <= limit) {
                auto handle = bgfx::createTexture2D(side, side, false, 1, bgfx::TextureFormat::RGBA8,
                                                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                if (!bgfx::isValid(handle))
                    throw std::runtime_error("Cannot allocate image atlas");
                imagePages_.push_back({handle});
                chosen = &imagePages_.back();
                imageBytes_ += side * side * 4;
            }
        }
        if (chosen) {
            const uint32_t w = image.width + 2, h = image.height + 2;
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    uint32_t sx = std::clamp(int(x) - 1, 0, int(image.width) - 1),
                             sy = std::clamp(int(y) - 1, 0, int(image.height) - 1);
                    std::memcpy(pixels.data() + (size_t(y) * w + x) * 4,
                                image.rgba.data() + (size_t(sy) * image.width + sx) * 4, 4);
                }
            texture.handle = chosen->handle;
            texture.pooled = true;
            texture.u0 = float(chosen->x + 1) / side;
            texture.v0 = float(chosen->y + 1) / side;
            texture.u1 = texture.u0 + float(image.width) / side;
            texture.v1 = texture.v0 + float(image.height) / side;
            bgfx::updateTexture2D(chosen->handle, 0, 0, uint16_t(chosen->x), uint16_t(chosen->y), uint16_t(w),
                                  uint16_t(h), bgfx::copy(pixels.data(), uint32_t(pixels.size())));
            stats_.uploadBytes += pixels.size();
            chosen->x += w;
            chosen->rowHeight = std::max(chosen->rowHeight, int(h));
        }
    }
    if (!bgfx::isValid(texture.handle)) {
        if (imageBytes_ + image.rgba.size() > limit)
            trimImages(true);
        if (imageBytes_ + image.rgba.size() > limit)
            throw std::runtime_error("GPU image budget exceeds 192 MiB");
        texture.handle =
            bgfx::createTexture2D(uint16_t(image.width), uint16_t(image.height), false, 1,
                                  bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                  bgfx::copy(image.rgba.data(), uint32_t(image.rgba.size())));
        if (!bgfx::isValid(texture.handle))
            throw std::runtime_error("Cannot allocate image texture");
        imageBytes_ += image.rgba.size();
        stats_.uploadBytes += image.rgba.size();
    }
    const uint64_t id = nextImage_++;
    try {
        images_.emplace(id, Image{texture, hash, 1, frame_});
        imageKeys_.emplace(key, id);
    } catch (...) {
        images_.erase(id);
        if (!texture.pooled) {
            bgfx::destroy(texture.handle);
            imageBytes_ -= image.rgba.size();
        }
        throw;
    }
    ++stats_.imageUploads;
    return {id};
}
size_t Renderer::imageTextureCount() const {
    size_t n = 0;
    for (const auto &[id, image] : images_)
        n += image.references > 0;
    return n;
}
void Renderer::releaseImage(ImageHandle handle) {
    auto it = images_.find(handle.id);
    if (it == images_.end() || !it->second.references)
        return;
    --it->second.references;
    it->second.lastUsed = frame_;
}
void Renderer::trimImages(bool allUnused) {
    auto expired = [&](const Image &image) {
        return !image.references && (allUnused || frame_ - image.lastUsed > 120);
    };
    auto remove = [&](auto it) {
        imageKeys_.erase({it->second.hash, {uint32_t(it->second.texture.w), uint32_t(it->second.texture.h)}});
        ++stats_.imageEvictions;
        return images_.erase(it);
    };
    for (auto it = images_.begin(); it != images_.end();) {
        if (expired(it->second)) {
            if (!it->second.texture.pooled) {
                flush();
                bgfx::destroy(it->second.texture.handle);
                imageBytes_ -= uint64_t(it->second.texture.w) * it->second.texture.h * 4;
                ++textureEpoch_;
            }
            it = remove(it);
        } else
            ++it;
    }
    for (auto page = imagePages_.begin(); page != imagePages_.end();) {
        bool used = false;
        for (const auto &[id, image] : images_)
            if (image.texture.pooled && image.texture.handle.idx == page->handle.idx && !expired(image)) {
                used = true;
                break;
            }
        if (used) {
            ++page;
            continue;
        }
        flush();
        for (auto it = images_.begin(); it != images_.end();)
            if (it->second.texture.pooled && it->second.texture.handle.idx == page->handle.idx)
                it = remove(it);
            else
                ++it;
        bgfx::destroy(page->handle);
        imageBytes_ -= 512 * 512 * 4;
        ++textureEpoch_;
        page = imagePages_.erase(page);
    }
}
void Renderer::image(Rect box, ImageHandle handle, ImageFit fit, uint32_t tint) {
    auto it = images_.find(handle.id);
    if (it == images_.end() || !it->second.references || !(tint >> 24))
        return;
    auto &t = it->second.texture;
    auto p = placeImage(box, uint32_t(t.w), uint32_t(t.h), fit);
    quad(p.bounds, t.handle, tint, t.u0 + (t.u1 - t.u0) * p.u1, t.u0 + (t.u1 - t.u0) * p.u0,
         t.v0 + (t.v1 - t.v0) * p.v1, t.v0 + (t.v1 - t.v0) * p.v0);
}
void Renderer::sprite(const std::array<float, 8> &points, ImageHandle handle, uint32_t tint, Rect uv) {
    auto found = images_.find(handle.id);
    if (found == images_.end() || !found->second.references) {
        spriteRaw(points, BGFX_INVALID_HANDLE, tint, uv);
        return;
    }
    const auto &t = found->second.texture;
    uv = {t.u0 + uv.x * (t.u1 - t.u0), t.v0 + uv.y * (t.v1 - t.v0), uv.w * (t.u1 - t.u0),
          uv.h * (t.v1 - t.v0)};
    spriteRaw(points, t.handle, tint, uv);
}
