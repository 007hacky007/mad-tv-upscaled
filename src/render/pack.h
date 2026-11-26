#pragma once
// The upscale pack: every image frame at 6x as RGBA PNG plus pack.json, built by tools/art/build_pack.py from
// the user's files. Images load on first use into a cache bounded in bytes (least recently used go first).
#include "data/result.h"
#include "render/provenance.h"
#include <cstdint>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <vector>
namespace mtv::render {
inline constexpr int kPackScale = 6;
struct UpscaledImage { int width = 0, height = 0; std::vector<uint8_t> rgba; };
class Pack {
public:
    // Checks pack.json: schema, scale, plain file names inside the pack directory, every listed file present.
    static Result<std::unique_ptr<Pack>> open(const std::filesystem::path& dir, size_t cache_bytes = size_t(512) << 20);
    bool contains(ImageRef r) const { return entries_.count(key(r)) != 0; }
    // nullptr when the image is not in the pack or its file is not 6x the listed size.
    const UpscaledImage* get(ImageRef r);
    bool cached(ImageRef r) const { return cache_.count(key(r)) != 0; }
    size_t size() const { return entries_.size(); }
private:
    struct Entry { int width = 0, height = 0; std::filesystem::path png; bool bad = false; };
    struct Cached { UpscaledImage image; std::list<uint32_t>::iterator lru; };
    static uint32_t key(ImageRef r) { return uint32_t(r.slot) << 16 | r.frame; }
    std::filesystem::path dir_;
    size_t limit_ = 0, used_ = 0;
    std::map<uint32_t, Entry> entries_;
    std::map<uint32_t, Cached> cache_;
    std::list<uint32_t> lru_; // front: most recently used
};
}
