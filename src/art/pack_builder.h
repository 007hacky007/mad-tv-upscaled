#pragma once
// Builds the upscale pack on the player's machine, as tools/art/build_pack.py: every image frame filled under its
// transparent pixels from opaque neighbours, nearest x2, Real-ESRGAN AnimeVideo v3 x3 (the bundled runner), the
// mask through the same method and thresholded at 128. Output <out>/<slot>-<frame>.png and pack.json.
#include "data/result.h"
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>
namespace mtv::art {
struct PackProgress { size_t done; size_t total; };
// The pack goes to <out_pack>.tmp and is renamed when complete; on error or cancel nothing is left at out_pack.
// `limit` > 0 builds only the first images (tests).
Result<void> build_pack(const std::filesystem::path& data_dir, const std::filesystem::path& runner,
                        const std::filesystem::path& models, const std::filesystem::path& out_pack,
                        const std::filesystem::path& work, std::function<void(PackProgress)> progress,
                        std::atomic<bool>& cancel, size_t limit = 0);
// The transparent-pixel fill of tools/art/upscale.py (dilate_into_transparent): RGBA in, RGB out.
std::vector<uint8_t> fill_transparent(const std::vector<uint8_t>& rgba, int width, int height);
}
