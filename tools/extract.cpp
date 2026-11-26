#include "extract.h"
#include "data/lzexe.h"
#include "data/powerpacker.h"
#include "intro/intro.h"
#include "data/rsc_codec.h"
#include <nlohmann/json.hpp>
#include "data/image.h"
#include "data/animation.h"
#include "render/palette.h"
#include "stb_image_write.h"
#include <filesystem>
#include <set>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
namespace mtv::tool {
int unpack_command(std::span<const std::string_view> args) {
    if (args.size() != 2) { std::cerr << "usage: mtv-tool unpack <in.exe> <out.exe>\n"; return 2; }
    std::ifstream in{std::string(args[0]), std::ios::binary};
    if (!in) { std::cerr << "cannot open " << args[0] << '\n'; return 1; }
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    auto out = unlzexe91(bytes);
    if (!out.has_value()) { std::cerr << args[0] << ": " << out.error().reason << " at " << out.error().byte_offset << '\n'; return 1; }
    std::ofstream o{std::string(args[1]), std::ios::binary};
    o.write(reinterpret_cast<const char*>(out.value().data()), std::streamsize(out.value().size()));
    if (!o) { std::cerr << "cannot write " << args[1] << '\n'; return 1; }
    return 0;
}
int unpack_pp20_command(std::span<const std::string_view> args) {
    if (args.size() != 2) { std::cerr << "usage: mtv-tool unpack-pp20 <in.pck> <out.bin>\n"; return 2; }
    std::ifstream in{std::string(args[0]), std::ios::binary};
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    auto out = unpack_pp20(bytes);
    if (!out.has_value()) { std::cerr << out.error().reason << '\n'; return 1; }
    std::ofstream o{std::string(args[1]), std::ios::binary};
    o.write(reinterpret_cast<const char*>(out.value().data()), std::streamsize(out.value().size()));
    return o ? 0 : 1;
}
int intro_frames_command(std::span<const std::string_view> args) {
    if (args.size() != 4 || args[0] != "--data" || args[2] != "--out") { std::cerr << "usage: mtv-tool intro-frames --data <dir> --out <dir>\n"; return 2; }
    auto data = IntroData::load(std::string(args[1]));
    if (!data.has_value()) { std::cerr << data.error().file << ": " << data.error().reason << '\n'; return 1; }
    std::filesystem::path out{std::string(args[3])};
    std::filesystem::create_directories(out);
    Intro intro(data.value());
    int n = 0;
    for (bool more = true; more;) {
        more = intro.step(false);
        const auto& f = intro.frame();
        if (n > 0) {
            std::vector<uint8_t> rgb(320 * 200 * 3);
            for (size_t i = 0; i < f.image.indices.size(); ++i) for (int c = 0; c < 3; ++c) rgb[i * 3 + size_t(c)] = f.rgb[f.image.indices[i]][size_t(c)];
            char name[32]; std::snprintf(name, sizeof name, "v%05d.png", n);
            stbi_write_png((out / name).string().c_str(), 320, 200, 3, rgb.data(), 320 * 3);
        }
        ++n;
    }
    std::cout << n << " retraces\n";
    return 0;
}
int classify_command(std::span<const std::string_view> args) {
    if (args.size() != 2 || args[0] != "--data") { std::cerr << "usage: mtv-tool classify --data <dir>\n"; return 2; }
    auto idx = ResourceIndex::open_dir(std::string(args[1]));
    if (!idx.has_value()) { std::cerr << idx.error().file << ": " << idx.error().reason << '\n'; return 1; }
    static const char* names[] = {"Palette", "Image", "Font", "Text", "Music", "Sound", "Table", "Driver", "Animation", "Unknown"};
    nlohmann::json out = nlohmann::json::array();
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        if (!e.has_value()) { std::cerr << "slot " << slot << ": " << e.error().reason << '\n'; return 1; }
        nlohmann::json j{{"slot", slot}, {"archive", idx.value().archive_of(slot)}, {"size", idx.value().get(slot).value().size()},
                         {"kind", names[int(e.value().kind)]}};
        if (e.value().kind == ResourceKind::Image) j["frames"] = e.value().image.frames.size();
        out.push_back(j);
    }
    std::cout << out.dump() << '\n';
    return 0;
}
int export_images_command(std::span<const std::string_view> args) {
    if (args.size() != 4 || args[0] != "--data" || args[2] != "--out") { std::cerr << "usage: mtv-tool export-images --data <dir> --out <dir>\n"; return 2; }
    auto idx = ResourceIndex::open_dir(std::string(args[1]));
    if (!idx.has_value()) { std::cerr << idx.error().reason << '\n'; return 1; }
    auto pal = parse_palette(idx.value().get(1).value());
    if (!pal.has_value()) { std::cerr << pal.error().reason << '\n'; return 1; }
    std::filesystem::path out{std::string(args[3])};
    std::filesystem::create_directories(out);
    nlohmann::json index = nlohmann::json::array();
    std::set<int> used;
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        if (!e.has_value() || e.value().kind != ResourceKind::Image) continue;
        auto frames = decode_image(e.value());
        if (!frames.has_value()) { std::cerr << "slot " << slot << ": " << frames.error().reason << '\n'; return 1; }
        for (size_t i = 0; i < frames.value().size(); ++i) {
            const auto& f = frames.value()[i];
            int high = 0;
            for (auto v : f.indices) { used.insert(v); if (v >= 32) ++high; }
            if (!f.width || !f.height) continue;
            auto rgba = to_rgba(f, pal.value(), kTransparentIndex);
            char name[64]; std::snprintf(name, sizeof name, "slot%03u-f%03zu.png", slot, i);
            if (!stbi_write_png((out / name).string().c_str(), f.width, f.height, 4, rgba.data(), f.width * 4)) { std::cerr << "write failed\n"; return 1; }
            index.push_back({{"slot", slot}, {"frame", i}, {"width", f.width}, {"height", f.height}, {"palette_slot", 1}, {"transparent_index", kTransparentIndex}, {"png", name}, {"pixels_above_31", high}});
        }
    }
    for (const auto& seq : kAnimationSequences) {
        auto back = decode_image(decode_rsc_entry(idx.value(), seq.backdrop).value());
        if (!back.has_value()) { std::cerr << "backdrop " << seq.backdrop << ": " << back.error().reason << '\n'; return 1; }
        for (auto slot : seq.parts) {
            if (!slot) continue;
            auto frames = decode_animation(decode_rsc_entry(idx.value(), slot).value().bytes, back.value()[0]);
            if (!frames.has_value()) { std::cerr << "animation " << slot << ": " << frames.error().reason << '\n'; return 1; }
            for (size_t i = 0; i < frames.value().size(); ++i) {
                const auto& f = frames.value()[i];
                auto rgba = to_rgba(f, pal.value(), std::nullopt);
                char name[64]; std::snprintf(name, sizeof name, "anim%03u-f%03zu.png", slot, i);
                stbi_write_png((out / name).string().c_str(), f.width, f.height, 4, rgba.data(), f.width * 4);
                index.push_back({{"animation", slot}, {"backdrop", seq.backdrop}, {"frame", i}, {"width", f.width}, {"height", f.height}, {"png", name}});
            }
        }
    }
    std::ofstream(out / "images.json") << index.dump(1) << '\n';
    std::cout << index.size() << " frames; indices used:";
    for (int v : used) std::cout << ' ' << v;
    std::cout << '\n';
    return 0;
}
}
