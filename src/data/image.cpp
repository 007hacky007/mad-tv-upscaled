#include "data/image.h"
namespace mtv {
Result<std::vector<IndexedImage>> decode_image(const DecodedEntry& e) {
    if (e.kind != ResourceKind::Image) return DataError{"rsc", "image", 0, "entry is not an image"};
    std::vector<IndexedImage> out;
    for (size_t i = 0; i < e.image.frames.size(); ++i) {
        const auto& f = e.image.frames[i];
        if (f.stride < f.width || f.pixels.size() < size_t(f.stride) * f.height)
            return DataError{"rsc", "image frame " + std::to_string(i), 0, "frame smaller than its dimensions"};
        IndexedImage img{f.width, f.height, {}};
        img.indices.reserve(size_t(f.width) * f.height);
        for (size_t y = 0; y < f.height; ++y)
            img.indices.insert(img.indices.end(), f.pixels.begin() + long(y * f.stride), f.pixels.begin() + long(y * f.stride + f.width));
        out.push_back(std::move(img));
    }
    return out;
}
}
