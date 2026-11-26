#pragma once
#include "data/result.h"
#include <cstdint>
#include <span>
#include <vector>
namespace mtv {
// PowerPacker 2.0 ("PP20") data files, as used for DATAx.PCK.
Result<std::vector<uint8_t>> unpack_pp20(std::span<const uint8_t> file);
}
