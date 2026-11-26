#include "game/provenance.h"
#include "data/rsc.h"
#include <algorithm>
namespace mtv::game {
namespace {
constexpr size_t kPrefix = 512; // packed bytes that identify a slot (frame table, tree sizes, start of the trees)
}
uint64_t ImageProvenance::key(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull ^ n;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
ImageProvenance::ImageProvenance(const std::filesystem::path& game_dir) {
    auto index = ResourceIndex::open_dir(game_dir);
    if (!index.has_value()) return;
    for (uint16_t slot : index.value().slots()) {
        auto e = index.value().get(slot);
        if (!e.has_value() || e.value().size() < 8) continue;
        const auto& b = e.value();
        const size_t n = std::min(b.size(), kPrefix);
        slot_by_prefix_.emplace(key(b.data(), n), slot);
        if (std::find(lengths_.begin(), lengths_.end(), n) == lengths_.end()) lengths_.push_back(n);
    }
    std::sort(lengths_.rbegin(), lengths_.rend());
}
void ImageProvenance::decoder_entry(const rt::Cpu& c) {
    // far call: [SP] return IP, [SP+2] return CS, [SP+4] input offset, [SP+6] input segment
    const auto& m = *c.m;
    const uint32_t sp = rt::Memory::lin(c.s[rt::SS], c.r[rt::SP]);
    const uint32_t in = rt::Memory::lin(m.r16(sp + 6), m.r16(sp + 4));
    slot_ = in < m.b.size() ? slot_of(&m.b[in], m.b.size() - in) : std::nullopt;
}
std::optional<uint16_t> ImageProvenance::slot_of(const uint8_t* p, size_t available) const {
    // entries shorter than the prefix are keyed by their whole length, so try every length the table holds
    for (size_t n : lengths_) {
        if (n > available) continue;
        if (auto it = slot_by_prefix_.find(key(p, n)); it != slot_by_prefix_.end()) return it->second;
    }
    return std::nullopt;
}
render::Tag ImageProvenance::pixel_tag(const rt::Cpu& c) {
    if (!slot_) return 0;
    const auto& m = *c.m;
    const uint16_t ds = c.s[rt::DS];
    const uint16_t frame = uint16_t(m.r16(rt::Memory::lin(ds, 0xA684)) - 1);
    const uint16_t x = m.r16(rt::Memory::lin(ds, 0xA672)), y = m.r16(rt::Memory::lin(ds, 0xA64C));
    auto [it, added] = ids_.try_emplace({*slot_, frame}, uint32_t(refs_.size()));
    if (added) {
        if (refs_.size() >= render::kMaxImageIds) { ids_.erase(it); return 0; }
        refs_.push_back({*slot_, frame});
    }
    return render::make_tag(it->second, x, y);
}
std::optional<render::ImageRef> ImageProvenance::image_of(render::Tag t) const {
    if (!render::tag_valid(t)) return std::nullopt;
    const uint32_t id = render::tag_image(t);
    if (id == 0 || id >= refs_.size()) return std::nullopt;
    return refs_[id];
}
}
