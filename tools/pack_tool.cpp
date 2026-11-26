// mtv-tool build-pack --data <game dir> --out <pack dir> [--work <dir>] [--runner <path>] [--limit N]
// The in-app pack builder from the command line (the default runner is the one under tools/upscalers).
#include "art/pack_builder.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
namespace mtv::tool {
int build_pack_command(std::span<const std::string_view> args) {
    std::filesystem::path data, out, work, runner = std::filesystem::path(MTV_SOURCE_ROOT) / "tools/upscalers/realesrgan-20220424/realesrgan-ncnn-vulkan";
    size_t limit = 0;
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        const std::string v(args[i + 1]);
        if (args[i] == "--data") data = v;
        else if (args[i] == "--out") out = v;
        else if (args[i] == "--work") work = v;
        else if (args[i] == "--runner") runner = v;
        else if (args[i] == "--limit") limit = size_t(std::strtoul(v.c_str(), nullptr, 10));
        else { std::cerr << "unknown option " << args[i] << '\n'; return 2; }
    }
    if (data.empty() || out.empty() || args.size() % 2) {
        std::cerr << "usage: mtv-tool build-pack --data <game dir> --out <pack dir> [--work <dir>] [--runner <path>] [--limit N]\n";
        return 2;
    }
    if (work.empty()) work = out.parent_path() / (out.filename().string() + "-work");
    std::atomic<bool> cancel{false};
    size_t shown = 0;
    auto r = art::build_pack(data, runner, runner.parent_path() / "models", out, work,
                             [&](art::PackProgress p) { if (p.done != shown) { shown = p.done; std::cerr << "\r" << p.done << " / " << p.total << std::flush; } },
                             cancel, limit);
    std::cerr << '\n';
    if (!r.has_value()) { std::cerr << r.error().file << ": " << r.error().reason << '\n'; return 1; }
    std::cout << "pack built at " << out.string() << '\n';
    return 0;
}
}
