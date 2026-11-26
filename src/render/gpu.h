#pragma once
#include "data/font.h"
#include "data/image.h"
#include "data/result.h"
#include "render/draw_list.h"
#include "render/palette.h"
#include "render/viewport.h"
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <filesystem>
#include <string_view>
namespace mtv {
// Supplies decoded frames to the renderer. The upscale pack (Task 20) plugs in here as well.
struct FrameSource {
    virtual ~FrameSource() = default;
    virtual std::optional<IndexedImage> frame(ResourceId id, uint16_t frame) const = 0;
    virtual const Palette& palette() const = 0;
    virtual const Font* font() const = 0;
};
// Composites the logical screen onto a 1920x1200 canvas (6x), then fits the canvas into the window.
class Renderer {
public:
    static Result<std::unique_ptr<Renderer>> create(SDL_Window* window, const FrameSource& source, std::string_view preference = {});
    ~Renderer();
    Result<void> render_canvas(const DrawList& list, GraphicsMode mode);
    // The game's own 320x200 RGB24 screen, scaled 6x with nearest neighbour onto the canvas.
    Result<void> render_screen(std::span<const uint8_t> rgb);
    // A composed 1920x1200 RGBA picture (render::Compositor); only the given area is uploaded again.
    Result<void> render_upscaled(std::span<const uint32_t> rgba, int x, int y, int w, int h);
    // Overlay drawing on the canvas, in logical 320x200 coordinates (6 canvas pixels each).
    void overlay_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void overlay_text(const std::string& ascii, int x, int y, float glyph_px);
    // With `capture`, also saves the composed window image as PNG before presenting it.
    Result<void> present(AspectMode aspect, const std::filesystem::path& capture = {});
    std::optional<std::array<uint8_t, 3>> canvas_pixel(int x, int y) const;
    std::string backend() const;
private:
    Renderer(SDL_Renderer* r, const FrameSource& s) : r_(r), source_(s) {}
    SDL_Texture* texture(const DrawCmd& c, GraphicsMode mode);
    void draw_text(const TextCmd& t, GraphicsMode mode);
    SDL_Renderer* r_ = nullptr;
    SDL_Texture* canvas_ = nullptr;
    SDL_Texture* screen_ = nullptr;
    SDL_Texture* upscaled_ = nullptr;
    const FrameSource& source_;
    std::map<TextureKey, SDL_Texture*> cache_;
    std::map<std::pair<uint8_t, uint8_t>, SDL_Texture*> glyphs_;
};
}
