#pragma once
// Every image frame of the game as RGBA (palette slot 1, colour 0 transparent): the input of the upscale pack.
#include "data/result.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace mtv::art {
struct ExportedFrame {
    uint16_t slot;
    uint16_t frame;
    int width, height;
    std::vector<uint8_t> rgba;
    std::string name() const; // slotNNN-fNNN.png, as mtv-tool export-images
};
Result<std::vector<ExportedFrame>> export_image_frames(const std::filesystem::path& data_dir);
}
