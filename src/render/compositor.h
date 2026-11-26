#pragma once
// Builds the 1920x1200 picture from the game's 320x200 frame. A pixel whose tag names an image in the pack is
// drawn as its 6x6 block of the upscaled image; where that block's upscaled mask is transparent, the picture
// continues the neighbouring pixel's image underneath (a sprite edge over a background). Other pixels are their
// palette colour, 6x6. Only blocks whose pixel, tag or a neighbour changed are redrawn.
#include "render/pack.h"
#include "render/provenance.h"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>
namespace mtv::render {
inline constexpr int kCanvasWidth = 320 * kPackScale, kCanvasHeight = 200 * kPackScale;
struct DirtyRect { int x = 0, y = 0, w = 0, h = 0; };
using Dac = std::array<std::array<uint8_t, 3>, 256>;
using ImageLookup = std::function<std::optional<ImageRef>(Tag)>;
class Compositor {
public:
    explicit Compositor(Pack* pack) : pack_(pack), canvas_(size_t(kCanvasWidth) * kCanvasHeight, 0xFF000000u) {}
    // Returns the canvas area that changed (empty when nothing did).
    DirtyRect update(std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of);
    const std::vector<uint32_t>& canvas() const { return canvas_; } // RGBA bytes in memory order (R first)
    void invalidate() { first_ = true; }
    void set_pack(Pack* pack) { pack_ = pack; first_ = true; } // a pack built while the game runs
private:
    void draw_block(int px, int py, std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of);
    void draw_own(int px, int py, uint32_t* out, uint32_t base, std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of);
    void overlay_neighbour_edges(int px, int py, uint32_t* out, std::span<const Tag> tags, const ImageLookup& image_of);
    Pack* pack_;
    std::vector<uint32_t> canvas_;
    std::vector<uint8_t> last_pixels_;
    std::vector<Tag> last_tags_;
    Dac last_dac_{};
    bool first_ = true;
};
}
