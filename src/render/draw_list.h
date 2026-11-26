#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace mtv {
// Resources are identified by their global RSC slot (the game merges all *.rsc tables). Animation frames use the
// animation slot.
struct ResourceId { uint16_t slot = 0; auto operator<=>(const ResourceId&) const = default; };
enum class DrawFlags : uint8_t { Opaque, Masked }; // Masked: index 0 is transparent (0x477AC and siblings)
enum class GraphicsMode : uint8_t { Nearest, Upscaled };
struct DrawCmd { ResourceId res; uint16_t frame = 0; int16_t x = 0, y = 0; uint8_t palette_id = 0; DrawFlags flags = DrawFlags::Masked; };
struct Rect { int16_t x = 0, y = 0, w = 0, h = 0; };
struct FillCmd { Rect rect; uint8_t colour = 0; };
struct TextCmd { std::string utf8; int16_t x = 0, y = 0; };
using DrawOp = std::variant<DrawCmd, FillCmd, TextCmd>;
// One frame of the logical 320x200 screen, in painter order.
class DrawList {
public:
    void blit(DrawCmd c) { ops_.emplace_back(c); }
    void fill(Rect r, uint8_t colour) { ops_.emplace_back(FillCmd{r, colour}); }
    void text(TextCmd t) { ops_.emplace_back(std::move(t)); }
    void clear() { ops_.clear(); }
    const std::vector<DrawOp>& ops() const { return ops_; }
private:
    std::vector<DrawOp> ops_;
};
struct TextureKey {
    uint16_t slot = 0, frame = 0; uint8_t palette_id = 0; GraphicsMode mode = GraphicsMode::Nearest;
    auto operator<=>(const TextureKey&) const = default;
};
TextureKey texture_key(const DrawCmd& c, GraphicsMode mode);
}
