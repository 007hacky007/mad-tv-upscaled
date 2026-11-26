#include "render/draw_list.h"
namespace mtv {
TextureKey texture_key(const DrawCmd& c, GraphicsMode mode) { return {c.res.slot, c.frame, c.palette_id, mode}; }
}
