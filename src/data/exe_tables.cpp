#include "data/exe_tables.h"
#include "data/mz.h"
#include <algorithm>
#include <cstring>
namespace mtv {
Result<ExeTables> read_exe_tables(std::span<const uint8_t> file) {
    auto mz = parse_mz(file);
    if (!mz.has_value()) return mz.error();
    const auto& img = mz.value().image;
    static const char sig[] = "Turbo-C - Copyright (c) 1988";
    auto it = std::search(img.begin(), img.end(), sig, sig + sizeof sig - 1);
    if (it == img.end() || (it - img.begin()) < 4 || ((it - img.begin()) - 4) % 16) return DataError{"MTV.EXE", "data segment", 0, "Turbo C data segment not found"};
    const size_t ds = size_t(it - img.begin()) - 4;
    if (ds + 0x22E + 256 > img.size()) return DataError{"MTV.EXE", "tables", ds, "data segment truncated"};
    ExeTables t;
    std::memcpy(t.glyph_map.data(), img.data() + ds + 0x12E, 256);
    std::memcpy(t.glyph_widths.data(), img.data() + ds + 0x22E, 256);
    return t;
}
}
