#include "art/pack_builder.h"
#include "art/export.h"
#include "data/hash.h"
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include "stb_image.h"
#include "stb_image_write.h"
namespace mtv::art {
namespace fs = std::filesystem;
namespace {
std::string u8(const fs::path& p) { const auto s = p.u8string(); return std::string(s.begin(), s.end()); }
DataError fail(const fs::path& where, std::string why) { return DataError{u8(where), "", 0, std::move(why)}; }
std::vector<uint8_t> nearest2(const std::vector<uint8_t>& src, int w, int h, int comp) {
    std::vector<uint8_t> out(size_t(w) * h * 4 * comp);
    for (int y = 0; y < h * 2; ++y)
        for (int x = 0; x < w * 2; ++x)
            for (int c = 0; c < comp; ++c) out[(size_t(y) * w * 2 + x) * comp + c] = src[(size_t(y / 2) * w + x / 2) * comp + c];
    return out;
}
// stbi_write_png takes a narrow path; on Windows that is the ANSI code page, so write through a stream instead.
bool write_png(const fs::path& p, int w, int h, int comp, const uint8_t* px) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    stbi_write_png_to_func([](void* ctx, void* data, int size) { static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size); },
                           &f, w, h, comp, px, w * comp);
    return bool(f);
}
std::vector<uint8_t> read_png(const fs::path& p, int comp, int& w, int& h) {
    std::ifstream f(p, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int n = 0;
    uint8_t* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h, &n, comp);
    if (!px) return {};
    std::vector<uint8_t> out(px, px + size_t(w) * h * comp);
    stbi_image_free(px);
    return out;
}
size_t count_png(const fs::path& d) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(d, ec)) if (e.path().extension() == ".png") ++n;
    return n;
}
// Runs the upscaler on a folder, reporting finished images; false with `why` on failure or cancel.
bool run_runner(const fs::path& runner, const fs::path& models, const fs::path& src, const fs::path& dst, std::atomic<bool>& cancel,
                const std::function<void(size_t)>& done, std::string& why) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    const std::string r = u8(runner), i = u8(src), o = u8(dst), m = u8(models);
    const char* args[] = {r.c_str(), "-i", i.c_str(), "-o", o.c_str(), "-n", "realesr-animevideov3", "-s", "3", "-m", m.c_str(), "-f", "png", nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
#ifdef _WIN32
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // no console window; the exit code is not reliable then, the output check below is
#endif
    SDL_Process* proc = fs::is_regular_file(runner, ec) ? SDL_CreateProcessWithProperties(props) : nullptr;
    SDL_DestroyProperties(props);
    if (!proc) { why = "cannot start the upscaler " + r; return false; }
    int code = -1;
    while (!SDL_WaitProcess(proc, false, &code)) {
        if (cancel) { SDL_KillProcess(proc, true); SDL_WaitProcess(proc, true, &code); SDL_DestroyProcess(proc); why = "cancelled"; return false; }
        if (done) done(count_png(dst));
        SDL_Delay(100);
    }
    SDL_DestroyProcess(proc);
    if (code != 0) { why = "the upscaler failed (exit code " + std::to_string(code) + ")"; return false; }
    if (done) done(count_png(dst));
    return true;
}
std::string file_hash(const fs::path& p) { auto h = sha256_file(p); return h.has_value() ? h.value() : ""; }
}
std::vector<uint8_t> fill_transparent(const std::vector<uint8_t>& rgba, int w, int h) {
    const size_t n = size_t(w) * h;
    std::vector<int32_t> rgb(n * 3);
    std::vector<uint8_t> known(n);
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
        known[i] = rgba[i * 4 + 3] > 0;
        count += known[i];
    }
    if (count != n && count != 0) {
        // numpy.roll semantics: neighbours wrap around the edges
        static constexpr int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        std::vector<int32_t> acc(n * 3);
        std::vector<int32_t> cnt(n);
        std::vector<uint8_t> grow(n);
        for (int round = 0; round < 64 && count != n; ++round) {
            std::fill(acc.begin(), acc.end(), 0);
            std::fill(cnt.begin(), cnt.end(), 0);
            for (const auto& d : dirs)
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const size_t from = size_t((y - d[0] + h) % h) * w + size_t((x - d[1] + w) % w), at = size_t(y) * w + x;
                        if (!known[from]) continue;
                        ++cnt[at];
                        for (int c = 0; c < 3; ++c) acc[at * 3 + c] += rgb[from * 3 + c];
                    }
            for (size_t i = 0; i < n; ++i) {
                grow[i] = !known[i] && cnt[i] > 0;
                if (grow[i]) for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = acc[i * 3 + c] / cnt[i];
            }
            for (size_t i = 0; i < n; ++i) if (grow[i]) { known[i] = 1; ++count; }
        }
    }
    std::vector<uint8_t> out(n * 3);
    for (size_t i = 0; i < n * 3; ++i) out[i] = uint8_t(rgb[i]);
    return out;
}
Result<void> build_pack(const fs::path& data_dir, const fs::path& runner, const fs::path& models, const fs::path& out_pack,
                        const fs::path& work, std::function<void(PackProgress)> progress, std::atomic<bool>& cancel, size_t limit) {
    if (cancel) return fail(out_pack, "cancelled");
    auto frames = export_image_frames(data_dir);
    if (!frames.has_value()) return frames.error();
    if (cancel) return fail(out_pack, "cancelled");
    auto& all = frames.value();
    std::sort(all.begin(), all.end(), [](const ExportedFrame& a, const ExportedFrame& b) { return a.name() < b.name(); });
    if (limit && all.size() > limit) all.resize(limit);
    const size_t total = all.size() * 2;
    std::error_code ec;
    const fs::path rgb = work / "rgb", mask = work / "mask", rgb_up = work / "rgb-up", mask_up = work / "mask-up";
    for (const auto& d : {rgb, mask, rgb_up, mask_up}) { fs::remove_all(d, ec); fs::create_directories(d, ec); if (ec) return fail(d, "cannot create: " + ec.message()); }
    for (const auto& f : all) {
        if (cancel) return fail(out_pack, "cancelled");
        const auto filled = fill_transparent(f.rgba, f.width, f.height);
        std::vector<uint8_t> alpha(size_t(f.width) * f.height * 3);
        for (size_t i = 0; i < size_t(f.width) * f.height; ++i) alpha[i * 3] = alpha[i * 3 + 1] = alpha[i * 3 + 2] = f.rgba[i * 4 + 3];
        if (!write_png(rgb / f.name(), f.width * 2, f.height * 2, 3, nearest2(filled, f.width, f.height, 3).data()) ||
            !write_png(mask / f.name(), f.width * 2, f.height * 2, 3, nearest2(alpha, f.width, f.height, 3).data()))
            return fail(work, "cannot write " + f.name());
    }
    std::string why;
    auto report = [&](size_t base) { return [&, base](size_t n) { if (progress) progress({base + std::min(n, all.size()), total}); }; };
    if (!run_runner(runner, models, rgb, rgb_up, cancel, report(0), why)) return fail(runner, why);
    if (!run_runner(runner, models, mask, mask_up, cancel, report(all.size()), why)) return fail(runner, why);
    fs::path tmp = out_pack;
    tmp += ".tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) return fail(tmp, "cannot create: " + ec.message());
    nlohmann::json images = nlohmann::json::array();
    for (const auto& f : all) {
        if (cancel) { fs::remove_all(tmp, ec); return fail(out_pack, "cancelled"); }
        int uw = 0, uh = 0, mw = 0, mh = 0;
        const auto up = read_png(rgb_up / f.name(), 3, uw, uh), m = read_png(mask_up / f.name(), 3, mw, mh);
        if (up.empty() || m.empty() || uw != f.width * 6 || uh != f.height * 6 || mw != uw || mh != uh) {
            fs::remove_all(tmp, ec);
            return fail(rgb_up / f.name(), "the upscaler produced no pictures (no Vulkan graphics driver?)");
        }
        std::vector<uint8_t> out(size_t(uw) * uh * 4);
        for (size_t i = 0; i < size_t(uw) * uh; ++i) {
            for (int c = 0; c < 3; ++c) out[i * 4 + c] = up[i * 3 + c];
            out[i * 4 + 3] = m[i * 3] >= 128 ? 255 : 0;
        }
        char name[32];
        std::snprintf(name, sizeof name, "%03u-%03u.png", unsigned(f.slot), unsigned(f.frame));
        if (!write_png(tmp / name, uw, uh, 4, out.data())) { fs::remove_all(tmp, ec); return fail(tmp / name, "cannot write"); }
        images.push_back({{"slot", f.slot}, {"frame", f.frame}, {"width", f.width}, {"height", f.height}, {"png", name}});
    }
    nlohmann::json pack{{"schema_version", 1}, {"scale", 6},
                        {"lock", {{"method", {{"pre", "nearest x2"}, {"model", "realesr-animevideov3"}, {"model_scale", 3}, {"mask", "same method, threshold 128"}}},
                                  {"scale", 6}, {"runner_sha256", file_hash(runner)},
                                  {"model_sha256", file_hash(models / "realesr-animevideov3-x3.bin")},
                                  {"param_sha256", file_hash(models / "realesr-animevideov3-x3.param")}}},
                        {"images", images}};
    std::ofstream(tmp / "pack.json") << pack.dump(1) << '\n';
    fs::path old = out_pack;
    old += ".old";
    fs::remove_all(old, ec);
    if (fs::exists(out_pack)) fs::rename(out_pack, old, ec);
    fs::rename(tmp, out_pack, ec);
    if (ec) return fail(out_pack, "cannot put the pack in place: " + ec.message());
    fs::remove_all(old, ec);
    return {};
}
}
