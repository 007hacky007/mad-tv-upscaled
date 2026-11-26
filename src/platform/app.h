#pragma once
#include "data/result.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace mtv {
struct AppOptions {
    bool smoke_test = false; std::uint32_t frames = 3; std::string report_path; std::string data_dir;
    std::string screenshot_path; // smoke test: save the last presented window image (PNG)
    std::string pack_dir;        // upscale pack (default: "pack" in the user directory)
    std::string import_dir;      // copy the original files (and a pack next to them) into the user folder, then exit
    bool intro = false;          // smoke test: play the intro first (a normal start always does)
    bool build_pack = false;     // smoke test: run the first-start art build (normal starts always do)
    bool realtime = false;       // smoke test: run the game by the real clock with (muted) audio, and report audio gaps
    std::string script_path;     // smoke test: input script, JSON list of {"frame", "keys" (BIOS codes), "mouse" [x, y], "buttons"}
};
Result<AppOptions> parse_app_options(std::span<const std::string_view> args);
int run_app(const AppOptions& options);
}
