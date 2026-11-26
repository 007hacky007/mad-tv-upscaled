#include "render/pack.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
namespace mtv::render {
Result<std::unique_ptr<Pack>> Pack::open(const std::filesystem::path& dir, size_t cache_bytes) {
    auto fail = [&](std::string why) -> Result<std::unique_ptr<Pack>> { return DataError{"pack.json", "", 0, std::move(why)}; };
    std::ifstream f(dir / "pack.json", std::ios::binary);
    if (!f) return fail("missing");
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("not a JSON object");
    if (j.value("schema_version", 0) != 1) return fail("unsupported schema");
    if (j.value("scale", 0) != kPackScale) return fail("scale is not 6");
    if (!j.contains("images") || !j["images"].is_array()) return fail("no image list");
    std::unique_ptr<Pack> p(new Pack);
    p->dir_ = dir;
    p->limit_ = cache_bytes;
    static const std::regex kName("[0-9]{3}-[0-9]{3}\\.png");
    for (const auto& e : j["images"]) {
        if (!e.is_object() || !e.contains("png") || !e["png"].is_string()) return fail("image entry without png");
        const std::string name = e["png"];
        if (!std::regex_match(name, kName)) return fail("file name not allowed: " + name);
        const int w = e.value("width", 0), h = e.value("height", 0), slot = e.value("slot", -1), frame = e.value("frame", -1);
        if (w <= 0 || h <= 0 || w > 320 || h > 200 || slot < 0 || slot > 0xFFFF || frame < 0 || frame > 0xFFFF) return fail("bad entry for " + name);
        if (!std::filesystem::is_regular_file(dir / name)) return fail("listed file missing: " + name);
        p->entries_[key({uint16_t(slot), uint16_t(frame)})] = {w, h, dir / name, false};
    }
    return p;
}
const UpscaledImage* Pack::get(ImageRef r) {
    const uint32_t k = key(r);
    if (auto it = cache_.find(k); it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return &it->second.image;
    }
    auto e = entries_.find(k);
    if (e == entries_.end() || e->second.bad) return nullptr;
    std::ifstream in(e->second.png, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    int w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
    if (!px || w != e->second.width * kPackScale || h != e->second.height * kPackScale) {
        if (px) stbi_image_free(px);
        e->second.bad = true;
        return nullptr;
    }
    UpscaledImage img{w, h, std::vector<uint8_t>(px, px + size_t(w) * h * 4)};
    stbi_image_free(px);
    const size_t bytes_needed = img.rgba.size();
    while (!lru_.empty() && used_ + bytes_needed > limit_) {
        const uint32_t old = lru_.back();
        lru_.pop_back();
        used_ -= cache_[old].image.rgba.size();
        cache_.erase(old);
    }
    lru_.push_front(k);
    used_ += bytes_needed;
    auto& c = cache_[k];
    c.image = std::move(img);
    c.lru = lru_.begin();
    return &c.image;
}
}
