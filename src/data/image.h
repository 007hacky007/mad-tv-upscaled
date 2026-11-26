#pragma once
#include "data/result.h"
#include "data/rsc_codec.h"
#include <cstdint>
#include <vector>
namespace mtv {
// One frame of colour indices, tightly packed (no row padding). MTV.EXE runs in VGA mode 13h, so indices are 0-255.
struct IndexedImage { uint16_t width = 0, height = 0; std::vector<uint8_t> indices; };
Result<std::vector<IndexedImage>> decode_image(const DecodedEntry& entry);
}
