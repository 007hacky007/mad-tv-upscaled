#include "coverage.h"
#include "data/animation.h"
#include "data/image.h"
#include "data/music.h"
#include "data/text.h"
#include "render/palette.h"
#include <algorithm>
#include <cstring>
namespace mtv {
const char* kind_name(ResourceKind k) {
    static const char* names[] = {"Palette", "Image", "Font", "Text", "Music", "Sound", "Table", "Driver", "Animation", "Unknown"};
    return names[int(k)];
}
ResourceKind slot_kind(uint16_t s, ResourceKind decoded) {
    if (s == 1 || s == 299) return ResourceKind::Palette;
    if (s == 95 || s == 98 || s == 99) return ResourceKind::Driver;
    if (s == 3) return ResourceKind::Font;
    if (s == 41 || text_layout_for_slot(s)) return ResourceKind::Text;
    if (s == 43 || s == 48 || s == 164 || (s >= 201 && s <= 206) || s == 208 || s == 209 || s == 210) return ResourceKind::Table;
    return decoded;
}
std::vector<CoverageEntry> build_coverage(const ResourceIndex& idx) {
    std::vector<CoverageEntry> out;
    for (auto slot : idx.slots()) {
        CoverageEntry c{slot, idx.archive_of(slot), ResourceKind::Unknown, false, ""};
        auto raw = idx.get(slot).value();
        auto e = decode_rsc_entry(idx, slot);
        if (!e.has_value()) { c.note = e.error().reason; out.push_back(c); continue; }
        c.kind = slot_kind(slot, e.value().kind);
        switch (c.kind) {
        case ResourceKind::Palette: { auto p = parse_palette(raw); c.decoded = p.has_value(); if (!c.decoded) c.note = p.error().reason; break; }
        case ResourceKind::Image: case ResourceKind::Font: { auto f = decode_image(e.value()); c.decoded = f.has_value(); if (!c.decoded) c.note = "image decode failed"; break; }
        case ResourceKind::Music: { auto s = decode_song(e.value().bytes); c.decoded = s.has_value(); if (!c.decoded) c.note = s.error().reason; break; }
        case ResourceKind::Animation: {
            auto seq = std::find_if(kAnimationSequences.begin(), kAnimationSequences.end(), [&](auto& q) { return std::find(q.parts.begin(), q.parts.end(), slot) != q.parts.end(); });
            if (seq == kAnimationSequences.end()) { c.note = "animation without a sequence"; break; }
            auto back = decode_image(decode_rsc_entry(idx, seq->backdrop).value());
            c.decoded = back.has_value() && decode_animation(e.value().bytes, back.value()[0]).has_value();
            if (!c.decoded) c.note = "animation decode failed";
            break;
        }
        case ResourceKind::Text: {
            if (slot == 41) { c.decoded = std::search(raw.begin(), raw.end(), "(D)eutsch", "(D)eutsch" + 9) != raw.end(); c.note = "language prompt"; break; }
            auto spec = text_layout_for_slot(slot);
            auto t = decode_text(raw, spec->layout, spec->block_entries);
            c.decoded = t.has_value(); if (!c.decoded) c.note = t.error().reason;
            break;
        }
        case ResourceKind::Driver: c.decoded = raw.size() > 12 && std::memcmp(raw.data(), "PALLADIX-PSM", 12) == 0; c.note = "Palladix driver; ported in Task 19"; break;
        case ResourceKind::Table: c.decoded = true; c.note = "raw game data, parsed by game logic"; break;
        default: c.note = "no decoder"; break;
        }
        out.push_back(c);
    }
    return out;
}
CoverageSummary summarize_coverage(const std::vector<CoverageEntry>& entries) {
    CoverageSummary s;
    for (auto& e : entries) {
        s.by_kind[kind_name(e.kind)]++;
        if (e.kind == ResourceKind::Unknown) ++s.unknown;
        if (!e.decoded) ++s.failed;
    }
    s.complete = s.unknown == 0 && s.failed == 0;
    return s;
}
}
