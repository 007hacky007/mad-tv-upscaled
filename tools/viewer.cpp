// mtv-tool viewer: browse every resource slot. Left/Right = slot, Up/Down = frame, 1/3/6 = zoom, Esc = quit.
// Text slots print their strings to stdout when selected; animations play over their backdrop.
#include "coverage.h"
#include "data/animation.h"
#include "data/image.h"
#include "data/text.h"
#include "render/palette.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <string_view>
#include <fstream>
#include <nlohmann/json.hpp>
namespace mtv::tool {
namespace {
std::vector<IndexedImage> frames_of(const ResourceIndex& idx, uint16_t slot, ResourceKind kind) {
    auto e = decode_rsc_entry(idx, slot);
    if (!e.has_value()) return {};
    if (kind == ResourceKind::Image || kind == ResourceKind::Font) {
        auto f = decode_image(e.value());
        return f.has_value() ? f.value() : std::vector<IndexedImage>{};
    }
    if (kind == ResourceKind::Animation)
        for (auto& q : kAnimationSequences)
            for (auto p : q.parts)
                if (p == slot) {
                    auto back = decode_image(decode_rsc_entry(idx, q.backdrop).value());
                    auto a = decode_animation(e.value().bytes, back.value()[0]);
                    return a.has_value() ? a.value() : std::vector<IndexedImage>{};
                }
    return {};
}
}
int viewer_command(std::span<const std::string_view> args) {
    if (args.size() != 2 || args[0] != "--data") { std::cerr << "usage: mtv-tool viewer --data <dir>\n"; return 2; }
    auto idx = ResourceIndex::open_dir(std::string(args[1]));
    if (!idx.has_value()) { std::cerr << idx.error().reason << '\n'; return 1; }
    auto pal = parse_palette(idx.value().get(1).value()).value();
    auto cov = build_coverage(idx.value());
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    SDL_Window* win = SDL_CreateWindow("Mad TV resources", 960, 600, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    size_t cur = 0, frame = 0; int zoom = 3; bool quit = false, dirty = true;
    std::vector<IndexedImage> frames;
    SDL_Texture* tex = nullptr;
    while (!quit) {
        if (dirty) {
            const auto& c = cov[cur];
            frames = frames_of(idx.value(), c.slot, c.kind);
            frame = std::min(frame, frames.empty() ? 0 : frames.size() - 1);
            std::string title = "slot " + std::to_string(c.slot) + " " + kind_name(c.kind) + " (" + c.archive + ")";
            if (!frames.empty()) title += " frame " + std::to_string(frame + 1) + "/" + std::to_string(frames.size());
            SDL_SetWindowTitle(win, title.c_str());
            if (tex) SDL_DestroyTexture(tex), tex = nullptr;
            if (!frames.empty() && frames[frame].width && frames[frame].height) {
                auto rgba = to_rgba(frames[frame], pal, c.kind == ResourceKind::Animation ? std::nullopt : std::optional<uint8_t>(kTransparentIndex));
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, frames[frame].width, frames[frame].height);
                SDL_UpdateTexture(tex, nullptr, rgba.data(), frames[frame].width * 4);
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            }
            if (c.kind == ResourceKind::Text && frame == 0) {
                auto spec = text_layout_for_slot(c.slot);
                if (spec) {
                    auto t = decode_text(idx.value().get(c.slot).value(), spec->layout, spec->block_entries);
                    if (t.has_value()) for (size_t i = 0; i < t.value().utf8.size(); ++i) std::cout << c.slot << '[' << i << "] " << t.value().utf8[i] << '\n';
                }
            }
            dirty = false;
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quit = true;
            if (ev.type != SDL_EVENT_KEY_DOWN) continue;
            switch (ev.key.key) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_RIGHT: cur = (cur + 1) % cov.size(); frame = 0; dirty = true; break;
            case SDLK_LEFT: cur = (cur + cov.size() - 1) % cov.size(); frame = 0; dirty = true; break;
            case SDLK_DOWN: if (frame + 1 < frames.size()) { ++frame; dirty = true; } break;
            case SDLK_UP: if (frame > 0) { --frame; dirty = true; } break;
            case SDLK_1: zoom = 1; break;
            case SDLK_3: zoom = 3; break;
            case SDLK_6: zoom = 6; break;
            default: break;
            }
        }
        SDL_SetRenderDrawColor(ren, 255, 0, 255, 255);
        SDL_RenderClear(ren);
        if (tex) { float w, h; SDL_GetTextureSize(tex, &w, &h); SDL_FRect r{8, 8, w * zoom, h * zoom}; SDL_RenderTexture(ren, tex, nullptr, &r); }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
int coverage_command(std::span<const std::string_view> args) {
    if (args.size() != 4 || args[0] != "--data" || args[2] != "--out") { std::cerr << "usage: mtv-tool coverage --data <dir> --out <file.json>\n"; return 2; }
    auto idx = ResourceIndex::open_dir(std::string(args[1]));
    if (!idx.has_value()) { std::cerr << idx.error().reason << '\n'; return 1; }
    auto entries = build_coverage(idx.value());
    auto sum = summarize_coverage(entries);
    nlohmann::json j{{"schema_version", 1}, {"complete", sum.complete}, {"unknown", sum.unknown}, {"failed", sum.failed}, {"by_kind", sum.by_kind}, {"slots", nlohmann::json::array()}};
    for (auto& e : entries) j["slots"].push_back({{"slot", e.slot}, {"archive", e.archive}, {"kind", kind_name(e.kind)}, {"decoded", e.decoded}, {"note", e.note}});
    std::ofstream(std::string(args[3])) << j.dump(1) << '\n';
    std::cout << j["by_kind"].dump() << " complete=" << sum.complete << '\n';
    return sum.complete ? 0 : 1;
}
}
