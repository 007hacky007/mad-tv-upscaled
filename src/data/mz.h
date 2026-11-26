#pragma once
#include "data/result.h"
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
namespace mtv {
struct MzHeader {
    uint16_t last_page_bytes{}, pages{}, relocation_count{}, header_paragraphs{}, minalloc{}, maxalloc{};
    uint16_t ss{}, sp{}, checksum{}, ip{}, cs{}, relocation_offset{}, overlay{};
};
struct MzImage {
    MzHeader header;
    std::vector<std::pair<uint16_t, uint16_t>> relocations; // (offset, segment)
    std::vector<uint8_t> image;
};
Result<MzImage> parse_mz(std::span<const uint8_t> file);
}
