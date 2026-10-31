#include "renderer.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <codecvt>
#include <locale>
#include "portable_font.hpp"
#endif
#include <bx/math.h>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cmath>
#include <bgfx/embedded_shader.h>
#include "shaders/vs_2d_dxbc.bin.h"
#include "shaders/vs_2d_essl.bin.h"
#include "fs_ocornut_imgui.bin.h"

namespace gm {
namespace {
const bgfx::EmbeddedShader shaders[] = {{"vs_2d",
                                         {{bgfx::RendererType::Direct3D11, vs_2d_dxbc, sizeof(vs_2d_dxbc)},
                                          {bgfx::RendererType::OpenGLES, vs_2d_essl, sizeof(vs_2d_essl)},
                                          {bgfx::RendererType::Count, nullptr, 0}}},
                                        BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
                                        BGFX_EMBEDDED_SHADER_END()};
bool numeric(const std::string &text) {
    return !text.empty() && text.size() <= 32 &&
           text.find_first_not_of("0123456789-+.%") == std::string::npos;
}
int glyphIndex(char ch) {
    return ch == '-' ? 10 : ch == '+' ? 11 : ch == '.' ? 12 : ch == '%' ? 13 : ch - '0';
}
} // namespace
uint32_t color(const std::string &hex) {
    const auto v = static_cast<uint32_t>(std::stoul(hex.substr(1), nullptr, 16));
    return ((v >> 16) & 255) | (v & 0xff00) | ((v & 255) << 16) | 0xff000000;
}
std::wstring wide(const std::string &text) {
#ifdef _WIN32
    if (text.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text);
#endif
}
std::string utf8(const std::wstring &text) {
#ifdef _WIN32
    if (text.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(text);
#endif
}
Renderer::Renderer(void *window, uint32_t width, uint32_t height, bgfx::CallbackI *callback, bool vsync)
    : width_(width), height_(height), resetFlags_(vsync?BGFX_RESET_VSYNC:0) {
    bgfx::Init init;
#ifdef _WIN32
    init.type = bgfx::RendererType::Direct3D11;
#else
    init.type = bgfx::RendererType::OpenGLES;
#endif
    init.callback = callback;
    init.swapChain.nwh = window;
    init.swapChain.width = width;
    init.swapChain.height = height;
    init.reset = resetFlags_;
    if (!bgfx::init(init))
        throw std::runtime_error("bgfx platform renderer initialization failed");
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    program_ = bgfx::createProgram(bgfx::createEmbeddedShader(shaders, init.type, "vs_2d"),
                                   bgfx::createEmbeddedShader(shaders, init.type, "fs_ocornut_imgui"), true);
    if (!bgfx::isValid(program_)) {
        bgfx::shutdown();
        throw std::runtime_error("Cannot create UI shader program");
    }
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    output_ = bgfx::createUniform("u_imguiOutput", bgfx::UniformType::Vec4);
    const uint32_t pixel = 0xffffffff;
    white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&pixel, 4));
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x090d14ff);
    bgfx::setViewName(0, "GM retained UI");
}
Renderer::~Renderer() {
    while (!geometry_.empty())
        releaseGeometry(geometry_.begin()->first);
    for (auto &[key, t] : images_)
        if (!t.texture.pooled)
            bgfx::destroy(t.texture.handle);
    for (auto &page : imagePages_)
        bgfx::destroy(page.handle);
    for (auto &[key, t] : textCache_)
        if (!t.pooled)
            bgfx::destroy(t.handle);
    for (auto &page : textPages_)
        bgfx::destroy(page.handle);
    for (auto &[key, a] : numberAtlases_)
        if (!a.texture.pooled)
            bgfx::destroy(a.texture.handle);
    bgfx::destroy(white_);
    bgfx::destroy(program_);
    bgfx::destroy(sampler_);
    bgfx::destroy(output_);
    bgfx::shutdown();
}
void Renderer::begin(uint32_t w, uint32_t h) {
    stats_ = {};
    activeView_=0; nextView_=1; nextPalette_=0; linearOutput_=false; layerBounds_={0,0,float(w),float(h)};layerWidth_=uint16_t(w);layerHeight_=uint16_t(h);
    bgfx::setViewFrameBuffer(0,BGFX_INVALID_HANDLE);
    ++frame_;
    frameStart_ = std::chrono::steady_clock::now();
    trimImages();
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        bgfx::SwapChain chain;
        chain.width = w;
        chain.height = h;
        bgfx::reset(resetFlags_, &chain);
    }
    if (textCache_.size() > 1024) {
        int removed = 0;
        for (auto it = textCache_.begin(); it != textCache_.end() && removed < 32;) {
            if (frame_ - it->second.lastUsed > 120) {
                if (!it->second.pooled) {
                    bgfx::destroy(it->second.handle);
                    ++textureEpoch_;
                }
                it = textCache_.erase(it);
                ++removed;
            } else
                ++it;
        }
    }
    float ortho[16];
    bx::mtxOrtho(ortho, 0, float(w), float(h), 0, 0, 1000, 0, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewRect(0, 0, 0, uint16_t(w), uint16_t(h));
    bgfx::setViewTransform(0, nullptr, ortho);
    bgfx::touch(0);
    unclip();
}
uint8_t Renderer::clearColor(const float* color){if(nextPalette_>=16)throw std::runtime_error("At most 16 scene viewports per frame");auto index=nextPalette_++;bgfx::setPaletteColor(index,color);return index;}
uint16_t Renderer::nextView(){flush();if(nextView_>=bgfx::getCaps()->limits.maxViews)throw std::runtime_error("Render view budget exceeded");auto view=nextView_++;bgfx::setViewClear(view,BGFX_CLEAR_NONE);bgfx::setViewMode(view,bgfx::ViewMode::Sequential);return view;}
void Renderer::layer2D(bgfx::FrameBufferHandle target,Rect logical,uint16_t w,uint16_t h,bool linear){activeView_=nextView();layerBounds_=logical;layerWidth_=w;layerHeight_=h;linearOutput_=linear;float ortho[16];bx::mtxOrtho(ortho,logical.x,logical.x+logical.w,logical.y+logical.h,logical.y,0,1000,0,bgfx::getCaps()->homogeneousDepth);bgfx::setViewFrameBuffer(activeView_,target);bgfx::setViewRect(activeView_,0,0,w,h);bgfx::setViewTransform(activeView_,nullptr,ortho);bgfx::setViewName(activeView_,"GM world overlay");}
void Renderer::backbuffer(){layer2D(BGFX_INVALID_HANDLE,{0,0,float(width_),float(height_)},uint16_t(width_),uint16_t(height_),false);}
void Renderer::end() {
    flush();
    stats_.textCacheEntries = uint32_t(textCache_.size());
    stats_.textAtlasPages = uint32_t(textPages_.size());
    stats_.textTextureBytes = textPages_.size() * 1024ull * 1024 * 4;
    for (const auto &[key, t] : textCache_)
        if (!t.pooled)
            stats_.textTextureBytes += uint64_t(t.w) * t.h * 4;
    stats_.imageTextureBytes = imageBytes_;
    stats_.imageEntries = uint32_t(images_.size());
    stats_.imageAtlasPages = uint32_t(imagePages_.size());
    stats_.imageTextures = stats_.imageAtlasPages;
    for (const auto &[id, image] : images_)
        stats_.imageTextures += !image.texture.pooled;
    stats_.geometryBytes = geometryBytes_;
    stats_.geometryEntries = uint32_t(geometry_.size());
    stats_.frame = frame_;
    stats_.backend = bgfx::getRendererName(bgfx::getRendererType());
    auto before = std::chrono::steady_clock::now();
    stats_.frontendMs = std::chrono::duration<double, std::milli>(before - frameStart_).count();
    bgfx::frame();
    stats_.presentWaitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
    if (const auto *counters = bgfx::getStats()) {
        auto duration = [](int64_t ticks, int64_t frequency) {
            return ticks >= 0 && frequency > 0 ? double(ticks) * 1000 / double(frequency) : -1.;
        };
        stats_.renderCpuMs = duration(counters->cpuTimeEnd - counters->cpuTimeBegin, counters->cpuTimerFreq);
        stats_.gpuMs = counters->gpuTimeBegin > 0
                           ? duration(counters->gpuTimeEnd - counters->gpuTimeBegin, counters->gpuTimerFreq)
                           : -1;
        stats_.waitRenderMs = duration(counters->waitRender, counters->cpuTimerFreq);
        stats_.waitSubmitMs = duration(counters->waitSubmit, counters->cpuTimerFreq);
        stats_.gpuFrame = counters->gpuFrameNum;
        stats_.backendDraws = counters->numDraw;
        stats_.gpuMemoryBytes = counters->gpuMemoryUsed;
        stats_.bgfxTextureBytes = counters->textureMemoryUsed;
    }
    completed_ = stats_;
}
void Renderer::clip(Rect r) {
    clipRect_ = r;
    clipEnabled_ = true;
}
void Renderer::unclip() {
    clipEnabled_ = false;
}
void Renderer::quad(Rect r, bgfx::TextureHandle texture, uint32_t c, float u1, float u0, float v1, float v0) {
    if (r.w <= 0 || r.h <= 0)
        return;
    // All UI primitives are axis-aligned quads. Clip geometry and UV together so
    // a different widget clip no longer forces a draw-call boundary.
    if (clipEnabled_) {
        const auto clipped = intersect(r, clipRect_);
        if (clipped.w <= 0 || clipped.h <= 0)
            return;
        const float du = u1 - u0, dv = v1 - v0, oldU = u0, oldV = v0;
        u0 = oldU + du * (clipped.x - r.x) / r.w;
        u1 = oldU + du * (clipped.x + clipped.w - r.x) / r.w;
        v0 = oldV + dv * (clipped.y - r.y) / r.h;
        v1 = oldV + dv * (clipped.y + clipped.h - r.y) / r.h;
        r = clipped;
    }
    if (texture.idx != batchTexture_.idx || vertices_.size() >= 4096) {
        flush();
        batchTexture_ = texture;
    }
    auto offset = uint16_t(vertices_.size());
    vertices_.insert(vertices_.end(), {{r.x, r.y, u0, v0, c},
                                       {r.x + r.w, r.y, u1, v0, c},
                                       {r.x + r.w, r.y + r.h, u1, v1, c},
                                       {r.x, r.y + r.h, u0, v1, c}});
    for (auto i : {0, 1, 2, 0, 2, 3})
        indices_.push_back(uint16_t(offset + i));
    ++batchQuads_;
}
#include "renderer_geometry.inl"
#include "renderer_images.inl"
void Renderer::rect(Rect r, uint32_t c) {
    // A reserved white texel in each text page lets ordered solids/text share a draw.
    if (!textPages_.empty()) {
        auto handle = textPages_.back().handle;
        for (auto &page : textPages_)
            if (page.handle.idx == batchTexture_.idx) {
                handle = page.handle;
                break;
            }
        constexpr float uv = .5f / 1024;
        quad(r, handle, c, uv, uv, uv, uv);
    } else
        quad(r, white_, c);
}
void Renderer::border(Rect r, uint32_t c, float t) {
    rect({r.x, r.y, r.w, t}, c);
    rect({r.x, r.y + r.h - t, r.w, t}, c);
    rect({r.x, r.y, t, r.h}, c);
    rect({r.x + r.w - t, r.y, t, r.h}, c);
}
void Renderer::spriteRaw(const std::array<float, 8> &points, bgfx::TextureHandle texture, uint32_t tint,
                         Rect uv) {
    if (!bgfx::isValid(texture)) {
        texture = white_;
        if (!textPages_.empty()) {
            texture = textPages_.back().handle;
            for (auto &page : textPages_)
                if (page.handle.idx == batchTexture_.idx) {
                    texture = page.handle;
                    break;
                }
            uv = {.5f / 1024, .5f / 1024, 0, 0};
        }
    }
    if (!(tint >> 24))
        return;
    std::array<Vertex, 12> polygon{}, scratch{};
    size_t count = 4;
    polygon[0] = {points[0], points[1], uv.x, uv.y, tint};
    polygon[1] = {points[2], points[3], uv.x + uv.w, uv.y, tint};
    polygon[2] = {points[4], points[5], uv.x + uv.w, uv.y + uv.h, tint};
    polygon[3] = {points[6], points[7], uv.x, uv.y + uv.h, tint};
    // Clip rotated quads in CPU space; retain batching and interpolate UVs.
    if (clipEnabled_)
        for (int side = 0; side < 4 && count; ++side) {
            size_t output = 0;
            float edge = side == 0   ? clipRect_.x
                         : side == 1 ? clipRect_.x + clipRect_.w
                         : side == 2 ? clipRect_.y
                                     : clipRect_.y + clipRect_.h;
            auto coordinate = [&](const Vertex &v) { return side < 2 ? v.x : v.y; };
            auto inside = [&](const Vertex &v) {
                return side % 2 ? coordinate(v) <= edge : coordinate(v) >= edge;
            };
            for (size_t i = 0; i < count; ++i) {
                auto a = polygon[i], b = polygon[(i + 1) % count];
                bool inA = inside(a), inB = inside(b);
                if (inA)
                    scratch[output++] = a;
                if (inA != inB) {
                    float t = (edge - coordinate(a)) / (coordinate(b) - coordinate(a));
                    scratch[output++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.u + (b.u - a.u) * t,
                                         a.v + (b.v - a.v) * t, tint};
                }
            }
            polygon = scratch;
            count = output;
        }
    if (count < 3)
        return;
    if (texture.idx != batchTexture_.idx || vertices_.size() + count >= 4096) {
        flush();
        batchTexture_ = texture;
    }
    auto base = uint16_t(vertices_.size());
    vertices_.insert(vertices_.end(), polygon.begin(), polygon.begin() + count);
    for (uint16_t i = 1; i + 1 < count; ++i) {
        indices_.push_back(base);
        indices_.push_back(base + i);
        indices_.push_back(base + i + 1);
    }
    ++batchQuads_;
}
Renderer::Texture Renderer::makeText(const std::string &s, int size) {
    ++stats_.textUploads;
#ifdef _WIN32
    const auto ws = wide(s);
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font =
        CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    auto oldFont = SelectObject(dc, font);
    SIZE measure{};
    GetTextExtentPoint32W(dc, ws.c_str(), int(ws.size()), &measure);
    const int w = std::clamp(int(measure.cx) + 4, 1, 4096), h = std::max(int(measure.cy) + 4, 1);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        throw std::runtime_error("Cannot allocate font bitmap");
    }
    auto oldBitmap = SelectObject(dc, bitmap);
    memset(pixels, 0, size_t(w) * h * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    TextOutW(dc, 0, 0, ws.c_str(), int(ws.size()));
    GdiFlush();
    auto *rgba = static_cast<uint32_t *>(pixels);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t a = rgba[i] & 255;
        rgba[i] = 0x00ffffff | (a << 24);
    }
    auto cleanup = [&]() {
        SelectObject(dc, oldBitmap);
        SelectObject(dc, oldFont);
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(dc);
    };
    try {
        auto texture = storeText(pixels, w, h);
        cleanup();
        return texture;
    } catch (...) {
        cleanup();
        throw;
    }
#else
    auto bitmap = rasterText(wide(s), size);
    return storeText(bitmap.pixels.data(), bitmap.width, bitmap.height);
#endif
}
Renderer::Texture Renderer::storeText(const void *pixels, int w, int h) {
    stats_.uploadBytes += uint64_t(w) * h * 4;
    constexpr int side = 1024;
    const uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (w + 2 > side || h + 2 > side)
        return {bgfx::createTexture2D(uint16_t(w), uint16_t(h), false, 1, bgfx::TextureFormat::RGBA8, flags,
                                      bgfx::copy(pixels, uint32_t(w * h * 4))),
                w, h, frame_};
    TextPage *chosen = nullptr;
    for (auto &page : textPages_) {
        if (page.x + w + 1 > side) {
            page.x = 1;
            page.y += page.rowHeight + 2;
            page.rowHeight = 0;
        }
        if (page.y + h + 1 <= side) {
            chosen = &page;
            break;
        }
    }
    if (!chosen) {
        if (textPages_.size() == 4) {
            auto oldest =
                std::min_element(textPages_.begin(), textPages_.end(),
                                 [](const auto &a, const auto &b) { return a.lastUsed < b.lastUsed; });
            flush();
            for (auto it = textCache_.begin(); it != textCache_.end();) {
                if (it->second.pooled && it->second.handle.idx == oldest->handle.idx)
                    it = textCache_.erase(it);
                else
                    ++it;
            }
            for (auto it = numberAtlases_.begin(); it != numberAtlases_.end();) {
                if (it->second.texture.handle.idx == oldest->handle.idx)
                    it = numberAtlases_.erase(it);
                else
                    ++it;
            }
            ++stats_.textEvictions;
            ++textureEpoch_;
            bgfx::destroy(oldest->handle);
            textPages_.erase(oldest);
        }
        std::vector<uint8_t> clear(side * side * 4, 0);
        std::fill_n(clear.begin(), 4, uint8_t(255));
        // Supplying initial memory makes a D3D11 texture immutable. Atlas pages
        // must be created without memory, then populated through updateTexture2D.
        auto handle = bgfx::createTexture2D(side, side, false, 1, bgfx::TextureFormat::RGBA8, flags);
        if (!bgfx::isValid(handle))
            throw std::runtime_error("Cannot allocate text atlas");
        bgfx::updateTexture2D(handle, 0, 0, 0, 0, side, side,
                              bgfx::copy(clear.data(), uint32_t(clear.size())));
        stats_.uploadBytes += clear.size();
        textPages_.push_back({handle});
        chosen = &textPages_.back();
    }
    bgfx::updateTexture2D(chosen->handle, 0, 0, uint16_t(chosen->x), uint16_t(chosen->y), uint16_t(w),
                          uint16_t(h), bgfx::copy(pixels, uint32_t(w * h * 4)));
    Texture result{chosen->handle,
                   w,
                   h,
                   frame_,
                   float(chosen->x) / side,
                   float(chosen->y) / side,
                   float(chosen->x + w) / side,
                   float(chosen->y + h) / side,
                   true};
    chosen->x += w + 2;
    chosen->rowHeight = std::max(chosen->rowHeight, h);
    chosen->lastUsed = frame_;
    return result;
}
Renderer::NumberAtlas Renderer::makeNumberAtlas(int size) {
#ifdef _WIN32
    ++stats_.textUploads;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font =
        CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    auto oldFont = SelectObject(dc, font);
    const std::wstring chars = L"0123456789-+.%";
    std::array<int, 14> advances{};
    int cell = 1, h = 1;
    for (int i = 0; i < 14; ++i) {
        SIZE measured{};
        GetTextExtentPoint32W(dc, &chars[i], 1, &measured);
        advances[i] = int(measured.cx);
        cell = std::max(cell, int(measured.cx) + 4);
        h = std::max(h, int(measured.cy) + 4);
    }
    const int w = cell * 14;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        throw std::runtime_error("Cannot create digit atlas");
    }
    auto oldBitmap = SelectObject(dc, bitmap);
    memset(pixels, 0, size_t(w) * h * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    for (int i = 0; i < 14; ++i)
        TextOutW(dc, i * cell, 0, &chars[i], 1);
    GdiFlush();
    auto *rgba = static_cast<uint32_t *>(pixels);
    for (int i = 0; i < w * h; ++i) {
        auto a = rgba[i] & 255;
        rgba[i] = 0xffffff | (a << 24);
    }
    auto cleanup = [&]() {
        SelectObject(dc, oldBitmap);
        SelectObject(dc, oldFont);
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(dc);
    };
    try {
        auto texture = storeText(pixels, w, h);
        cleanup();
        return {texture, cell, advances};
    } catch (...) {
        cleanup();
        throw;
    }
#else
    ++stats_.textUploads;
    const std::wstring chars = L"0123456789-+.%";
    std::array<int, 14> advances{};
    std::array<FontBitmap, 14> glyphs;
    int cell = 1, h = 1;
    for (size_t i = 0; i < 14; ++i) {
        glyphs[i] = rasterText(chars.substr(i, 1), size);
        advances[i] = glyphs[i].width - 4;
        cell = std::max(cell, glyphs[i].width);
        h = std::max(h, glyphs[i].height);
    }
    const int w = cell * 14;
    std::vector<uint32_t> pixels(size_t(w) * h, 0);
    for (int i = 0; i < 14; ++i)
        for (int y = 0; y < glyphs[i].height; ++y)
            std::copy_n(glyphs[i].pixels.data() + y * glyphs[i].width, glyphs[i].width,
                        pixels.data() + y * w + i * cell);
    return {storeText(pixels.data(), w, h), cell, advances};
#endif
}
Renderer::NumberAtlas &Renderer::numberAtlas(int size) {
    auto it = numberAtlases_.find(size);
    if (it == numberAtlases_.end())
        it = numberAtlases_.emplace(size, makeNumberAtlas(size)).first;
    return it->second;
}
float Renderer::textRun(float x, float y, const std::string &s, int px, uint32_t c, float maxWidth) {
    if (numeric(s)) {
        const auto &atlas = numberAtlas(px);
        float offset = 0;
        for (char ch : s) {
            auto index = glyphIndex(ch);
            float w = std::min(float(atlas.advances[index] + 4), maxWidth - offset);
            if (w <= 0)
                break;
            const auto &t = atlas.texture;
            const float u0 = t.u0 + (t.u1 - t.u0) * float(index * atlas.cellWidth) / t.w,
                        u1 = u0 + (t.u1 - t.u0) * w / t.w;
            for (auto &page : textPages_)
                if (page.handle.idx == t.handle.idx)
                    page.lastUsed = frame_;
            quad({x + offset, y, w, float(t.h)}, t.handle, c, u1, u0, t.v1, t.v0);
            offset += atlas.advances[index];
        }
        return offset;
    }
    auto key = std::to_string(px) + ":" + s;
    auto it = textCache_.find(key);
    if (it == textCache_.end())
        it = ([&] {
            trimTextCache();
            return textCache_.emplace(key, makeText(s, px)).first;
        })();
    it->second.lastUsed = frame_;
    const auto &t = it->second;
    const float width = std::min(float(t.w), maxWidth);
    for (auto &page : textPages_)
        if (page.handle.idx == t.handle.idx)
            page.lastUsed = frame_;
    quad({x, y, width, float(t.h)}, t.handle, c, t.u0 + (t.u1 - t.u0) * width / t.w, t.u0, t.v1, t.v0);
    return float(t.w) - 4;
}
float Renderer::runWidth(const std::string &s, int px) {
    if (numeric(s)) {
        const auto &atlas = numberAtlas(px);
        float width = 0;
        for (char ch : s)
            width += atlas.advances[glyphIndex(ch)];
        return width;
    }
    auto key = std::to_string(px) + ":" + s;
    auto it = textCache_.find(key);
    if (it == textCache_.end())
        it = ([&] {
            trimTextCache();
            return textCache_.emplace(key, makeText(s, px)).first;
        })();
    it->second.lastUsed = frame_;
    return float(it->second.w) - 4;
}
namespace {
template <class F> void textRuns(const std::string &s, F fn) {
    size_t first = 0;
    while (first < s.size()) {
        bool digit = s[first] >= '0' && s[first] <= '9';
        size_t end = first + 1;
        if (digit)
            while (end < s.size() && ((s[end] >= '0' && s[end] <= '9') || s[end] == '.' || s[end] == '%'))
                ++end;
        else
            while (end < s.size() && !(s[end] >= '0' && s[end] <= '9'))
                ++end;
        if (!fn(s.substr(first, end - first)))
            break;
        first = end;
    }
}
} // namespace
void Renderer::text(float x, float y, const std::string &s, float size, uint32_t c, float maxWidth) {
    if (s.empty() || size < 3 || maxWidth <= 0)
        return;
    int px = std::clamp(int(size + .5f), 8, 72);
    float offset = 0;
    if (numeric(s)) {
        textRun(x, y, s, px, c, maxWidth);
        return;
    }
    textRuns(s, [&](const std::string &run) {
        if (offset >= maxWidth)
            return false;
        offset += textRun(x + offset, y, run, px, c, maxWidth - offset);
        return true;
    });
}
float Renderer::textWidth(const std::string &s, float size) {
    if (s.empty())
        return 0;
    int px = std::clamp(int(size + .5f), 8, 72);
    float width = 0;
    if (numeric(s))
        return runWidth(s, px);
    textRuns(s, [&](const std::string &run) {
        width += runWidth(run, px);
        return true;
    });
    return width;
}
void Renderer::trimTextCache() {
    uint64_t largeBytes = 0;
    for (const auto &[key, t] : textCache_)
        if (!t.pooled)
            largeBytes += uint64_t(t.w) * t.h * 4;
    while (textCache_.size() >= 2048 || largeBytes > 24 * 1024 * 1024) {
        auto oldest =
            std::min_element(textCache_.begin(), textCache_.end(), [](const auto &a, const auto &b) {
                return a.second.lastUsed < b.second.lastUsed;
            });
        if (oldest == textCache_.end())
            break;
        if (!oldest->second.pooled) {
            flush();
            largeBytes -= uint64_t(oldest->second.w) * oldest->second.h * 4;
            bgfx::destroy(oldest->second.handle);
            ++textureEpoch_;
        }
        textCache_.erase(oldest);
        ++stats_.textEvictions;
    }
}

} // namespace gm
