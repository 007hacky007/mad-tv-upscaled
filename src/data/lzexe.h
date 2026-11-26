#pragma once
#include "data/result.h"
#include <cstdint>
#include <span>
#include <vector>
namespace mtv {
// Unpacks an LZEXE 0.91 compressed DOS executable into a complete, runnable MZ file.
Result<std::vector<uint8_t>> unlzexe91(std::span<const uint8_t> file);
}
