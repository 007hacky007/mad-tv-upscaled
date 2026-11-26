#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <span>
namespace mtv {
// Small tables read from the user's own MTV.EXE at import time, so no game data is compiled into the port.
struct ExeTables {
    std::array<uint8_t, 256> glyph_map{};    // DS:012E
    std::array<uint8_t, 256> glyph_widths{}; // DS:022E
};
// `unpacked_mz` is MTV.EXE after unlzexe91. The data segment is located through the Turbo C signature at DS:0004.
Result<ExeTables> read_exe_tables(std::span<const uint8_t> unpacked_mz);
}
