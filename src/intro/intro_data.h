#pragma once
#include "data/image.h"
#include "data/result.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>
namespace mtv {
// Everything the intro needs, read from the user's own INTRO.EXE, MCV.BIN and DATAV.PCK.
struct IntroData {
    std::vector<uint8_t> datav;                        // unpacked DATAV.PCK (25000 bytes)
    std::array<std::array<uint8_t, 3>, 17> dac{};      // MCV data 0x34: DAC block for INT 10h AX=1012h
    std::array<uint8_t, 17> attr{};                    // MCV data 0x67: attribute palette registers
    std::array<uint16_t, 10> spark_offset{};           // MCV data 0x20 (in 4-byte units of DATAV)
    std::array<uint8_t, 8> bit{};                      // MCV data 0x78: 0x80 >> n
    std::array<uint8_t, 7> layer_add{};                // MCV data 0x80: high-byte add per fire layer
    std::array<uint16_t, 53> tile_dest{}, tile_src{};  // MCV data 0x87 / 0xF1
    std::array<uint16_t, 27> logo_step{};              // INTRO DS 0x96
    std::array<uint16_t, 5> phase_count{};             // INTRO DS 0xCC
    std::array<int16_t, 5> phase_dx{}, phase_dy{};     // INTRO DS 0xD6 / 0xE0
    std::array<uint8_t, 7> layer_delay{};              // INTRO DS 0xEA
    std::array<uint8_t, 53> tile_height{};             // INTRO DS 0xF8 (bit 7 = 16 pixels wide)
    static Result<IntroData> load(const std::filesystem::path& game_dir);
    // Component pictures for the upscaler, in the intro palette (index 0 = transparent for spark/fire).
    IndexedImage spark(int frame) const;
    IndexedImage tile(int layer, int step) const;
    IndexedImage logo() const;
};
}
