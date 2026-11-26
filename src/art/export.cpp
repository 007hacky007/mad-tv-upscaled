#include "art/export.h"
#include "data/image.h"
#include "data/rsc.h"
#include "data/rsc_codec.h"
#include "render/palette.h"
#include <cstdio>
namespace mtv::art {
std::string ExportedFrame::name() const {
    char b[32];
    std::snprintf(b, sizeof b, "slot%03u-f%03u.png", unsigned(slot), unsigned(frame));
    return b;
}
Result<std::vector<ExportedFrame>> export_image_frames(const std::filesystem::path& data_dir) {
    auto idx = ResourceIndex::open_dir(data_dir);
    if (!idx.has_value()) return idx.error();
    auto raw_pal = idx.value().get(1);
    if (!raw_pal.has_value()) return raw_pal.error();
    auto pal = parse_palette(raw_pal.value());
    if (!pal.has_value()) return pal.error();
    std::vector<ExportedFrame> out;
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        if (!e.has_value() || e.value().kind != ResourceKind::Image) continue;
        auto frames = decode_image(e.value());
        if (!frames.has_value()) return frames.error();
        for (size_t i = 0; i < frames.value().size(); ++i) {
            const auto& f = frames.value()[i];
            if (!f.width || !f.height) continue;
            out.push_back({slot, uint16_t(i), f.width, f.height, to_rgba(f, pal.value(), kTransparentIndex)});
        }
    }
    return out;
}
}
