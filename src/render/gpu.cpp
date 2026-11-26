#include "render/gpu.h"
#include "data/text.h"
#include <string>
namespace mtv {
namespace {
constexpr int kCanvasW = kLogicalWidth * kArtScale, kCanvasH = kLogicalHeight * kArtScale;
SDL_Texture* upload(SDL_Renderer* r, const IndexedImage& img, const Palette& pal, bool masked) {
    if (!img.width || !img.height) return nullptr;
    auto rgba = to_rgba(img, pal, masked ? std::optional<uint8_t>(kTransparentIndex) : std::nullopt);
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, rgba.data(), img.width * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}
}
Result<std::unique_ptr<Renderer>> Renderer::create(SDL_Window* w, const FrameSource& s, std::string_view preference) {
    SDL_Renderer* r = SDL_CreateRenderer(w, preference.empty() ? nullptr : std::string(preference).c_str());
    if (!r) return DataError{"renderer", "", 0, SDL_GetError()};
    std::unique_ptr<Renderer> out(new Renderer(r, s));
    out->canvas_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, kCanvasW, kCanvasH);
    if (!out->canvas_) return DataError{"renderer", "canvas", 0, SDL_GetError()};
    SDL_SetTextureScaleMode(out->canvas_, SDL_SCALEMODE_LINEAR);
    return out;
}
Renderer::~Renderer() {
    for (auto& [k, t] : cache_) SDL_DestroyTexture(t);
    for (auto& [k, t] : glyphs_) SDL_DestroyTexture(t);
    if (canvas_) SDL_DestroyTexture(canvas_);
    if (screen_) SDL_DestroyTexture(screen_);
    if (upscaled_) SDL_DestroyTexture(upscaled_);
    if (r_) SDL_DestroyRenderer(r_);
}
std::string Renderer::backend() const { return SDL_GetRendererName(r_); }
SDL_Texture* Renderer::texture(const DrawCmd& c, GraphicsMode mode) {
    // Masked and opaque draws of the same frame need different alpha, so the flag is part of the key.
    TextureKey key = texture_key(c, mode);
    key.palette_id = uint8_t(key.palette_id * 2 + (c.flags == DrawFlags::Masked));
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    auto img = source_.frame(c.res, c.frame);
    SDL_Texture* t = img ? upload(r_, *img, source_.palette(), c.flags == DrawFlags::Masked) : nullptr;
    cache_[key] = t;
    return t;
}
void Renderer::draw_text(const TextCmd& t, GraphicsMode) {
    const Font* f = source_.font();
    if (!f) return;
    int x = t.x;
    for (unsigned char c : utf8_to_cp437(t.utf8)) {
        if (c == ' ') { x += f->space_advance; continue; }
        const auto& g = f->glyphs[c].has_value() ? f->glyphs[c] : f->glyphs['.'];
        if (!g) continue;
        auto key = std::make_pair(c, uint8_t(0));
        SDL_Texture*& tex = glyphs_[key];
        if (!tex) tex = upload(r_, g->image, source_.palette(), true);
        if (tex) { SDL_FRect d{float(x * kArtScale), float(t.y * kArtScale), float(g->image.width * kArtScale), float(g->image.height * kArtScale)}; SDL_RenderTexture(r_, tex, nullptr, &d); }
        x += g->advance;
    }
}
Result<void> Renderer::render_canvas(const DrawList& list, GraphicsMode mode) {
    if (!SDL_SetRenderTarget(r_, canvas_)) return DataError{"renderer", "target", 0, SDL_GetError()};
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    for (const auto& op : list.ops()) {
        if (auto* d = std::get_if<DrawCmd>(&op)) {
            SDL_Texture* t = texture(*d, mode);
            if (!t) continue;
            float w, h; SDL_GetTextureSize(t, &w, &h);
            SDL_FRect r{float(d->x * kArtScale), float(d->y * kArtScale), w * kArtScale, h * kArtScale};
            SDL_RenderTexture(r_, t, nullptr, &r);
        } else if (auto* f = std::get_if<FillCmd>(&op)) {
            const auto& c = source_.palette().rgb6[f->colour];
            SDL_SetRenderDrawColor(r_, dac_to_8bit(c[0]), dac_to_8bit(c[1]), dac_to_8bit(c[2]), 255);
            SDL_FRect r{float(f->rect.x * kArtScale), float(f->rect.y * kArtScale), float(f->rect.w * kArtScale), float(f->rect.h * kArtScale)};
            SDL_RenderFillRect(r_, &r);
        } else if (auto* t = std::get_if<TextCmd>(&op)) {
            draw_text(*t, mode);
        }
    }
    SDL_SetRenderTarget(r_, nullptr);
    return {};
}
Result<void> Renderer::render_screen(std::span<const uint8_t> rgb) {
    if (rgb.size() != size_t(kLogicalWidth * kLogicalHeight * 3)) return DataError{"renderer", "screen", 0, "screen must be 320x200 RGB24"};
    if (!screen_) {
        screen_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kLogicalWidth, kLogicalHeight);
        if (!screen_) return DataError{"renderer", "screen", 0, SDL_GetError()};
        SDL_SetTextureScaleMode(screen_, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(screen_, SDL_BLENDMODE_NONE);
    }
    if (!SDL_UpdateTexture(screen_, nullptr, rgb.data(), kLogicalWidth * 3)) return DataError{"renderer", "screen", 0, SDL_GetError()};
    if (!SDL_SetRenderTarget(r_, canvas_)) return DataError{"renderer", "target", 0, SDL_GetError()};
    SDL_RenderTexture(r_, screen_, nullptr, nullptr);
    SDL_SetRenderTarget(r_, nullptr);
    return {};
}
Result<void> Renderer::render_upscaled(std::span<const uint32_t> rgba, int x, int y, int w, int h) {
    if (rgba.size() != size_t(kCanvasW) * kCanvasH) return DataError{"renderer", "upscaled", 0, "picture must be 1920x1200"};
    if (!upscaled_) {
        upscaled_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, kCanvasW, kCanvasH);
        if (!upscaled_) return DataError{"renderer", "upscaled", 0, SDL_GetError()};
        SDL_SetTextureBlendMode(upscaled_, SDL_BLENDMODE_NONE);
        x = 0; y = 0; w = kCanvasW; h = kCanvasH;
    }
    if (w > 0 && h > 0) {
        const SDL_Rect r{x, y, w, h};
        if (!SDL_UpdateTexture(upscaled_, &r, rgba.data() + size_t(y) * kCanvasW + x, kCanvasW * 4)) return DataError{"renderer", "upscaled", 0, SDL_GetError()};
    }
    if (!SDL_SetRenderTarget(r_, canvas_)) return DataError{"renderer", "target", 0, SDL_GetError()};
    SDL_RenderTexture(r_, upscaled_, nullptr, nullptr);
    SDL_SetRenderTarget(r_, nullptr);
    return {};
}
void Renderer::overlay_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderTarget(r_, canvas_);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, r, g, b, a);
    const SDL_FRect f{float(x * kArtScale), float(y * kArtScale), float(w * kArtScale), float(h * kArtScale)};
    SDL_RenderFillRect(r_, &f);
    SDL_SetRenderTarget(r_, nullptr);
}
void Renderer::overlay_text(const std::string& ascii, int x, int y, float glyph_px) {
    // SDL's built-in 8x8 font, scaled to glyph_px canvas pixels
    SDL_SetRenderTarget(r_, canvas_);
    const float s = glyph_px / float(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
    SDL_SetRenderScale(r_, s, s);
    SDL_SetRenderDrawColor(r_, 255, 255, 255, 255);
    SDL_RenderDebugText(r_, float(x * kArtScale) / s, float(y * kArtScale) / s, ascii.c_str());
    SDL_SetRenderScale(r_, 1, 1);
    SDL_SetRenderTarget(r_, nullptr);
}
Result<void> Renderer::present(AspectMode aspect, const std::filesystem::path& capture) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    auto v = Viewport::fit(w, h, aspect);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    SDL_FRect dst{float(v.x), float(v.y), float(v.width), float(v.height)};
    SDL_RenderTexture(r_, canvas_, nullptr, &dst);
    if (!capture.empty()) {
        SDL_Surface* s = SDL_RenderReadPixels(r_, nullptr);
        const bool ok = s && SDL_SavePNG(s, reinterpret_cast<const char*>(capture.u8string().c_str()));
        if (s) SDL_DestroySurface(s);
        if (!ok) return DataError{"renderer", "capture", 0, SDL_GetError()};
    }
    if (!SDL_RenderPresent(r_)) return DataError{"renderer", "present", 0, SDL_GetError()};
    return {};
}
std::optional<std::array<uint8_t, 3>> Renderer::canvas_pixel(int x, int y) const {
    if (!SDL_SetRenderTarget(r_, canvas_)) return std::nullopt;
    SDL_Rect rect{x, y, 1, 1};
    SDL_Surface* s = SDL_RenderReadPixels(r_, &rect);
    SDL_SetRenderTarget(r_, nullptr);
    if (!s) return std::nullopt;
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return std::nullopt;
    auto* p = static_cast<uint8_t*>(c->pixels);
    std::array<uint8_t, 3> out{p[0], p[1], p[2]};
    SDL_DestroySurface(c);
    return out;
}
}
