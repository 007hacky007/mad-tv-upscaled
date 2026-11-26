#include "render/compositor.h"
#include <algorithm>
namespace mtv::render {
namespace {
uint32_t flat(const Dac& dac, uint8_t v) {
    auto e = [](uint8_t c) { return uint32_t(c << 2 | c >> 4); };
    return e(dac[v][0]) | e(dac[v][1]) << 8 | e(dac[v][2]) << 16 | 0xFF000000u;
}
uint32_t texel(const UpscaledImage& img, int x, int y) {
    const uint8_t* p = &img.rgba[(size_t(y) * img.width + x) * 4];
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}
void Compositor::draw_block(int px, int py, std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of) {
    const size_t i = size_t(py) * 320 + px;
    uint32_t* out = &canvas_[size_t(py) * kPackScale * kCanvasWidth + size_t(px) * kPackScale];
    const uint32_t base = flat(dac, pixels[i]);
    auto fill = [&](uint32_t c) { for (int y = 0; y < kPackScale; ++y) std::fill_n(out + size_t(y) * kCanvasWidth, kPackScale, c); };
    if (!pack_) { fill(base); return; }
    draw_own(px, py, out, base, pixels, tags, dac, image_of);
    overlay_neighbour_edges(px, py, out, tags, image_of);
}
void Compositor::overlay_neighbour_edges(int px, int py, uint32_t* out, std::span<const Tag> tags, const ImageLookup& image_of) {
    // A neighbour's image continued into this pixel: where its upscaled block is partly opaque, it is that
    // image's smoothed edge reaching over a pixel that was transparent in the source, so it goes on top. A fully
    // opaque block means the image covered this pixel at the source and whatever shows here was drawn over it.
    static constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, -1, 0, 1};
    const Tag own = tags[size_t(py) * 320 + px];
    for (int k = 0; k < 4; ++k) {
        const int qx = px + kDx[k], qy = py + kDy[k];
        if (qx < 0 || qy < 0 || qx >= 320 || qy >= 200) continue;
        const Tag t = tags[size_t(qy) * 320 + qx];
        if (!tag_valid(t) || (tag_valid(own) && tag_image(own) == tag_image(t))) continue;
        const auto ref = image_of(t);
        const UpscaledImage* img = ref ? pack_->get(*ref) : nullptr;
        if (!img) continue;
        const auto [qsx, qsy] = tag_xy(t);
        const int cx = int(qsx) - kDx[k], cy = int(qsy) - kDy[k];
        if (cx < 0 || cy < 0 || (cx + 1) * kPackScale > img->width || (cy + 1) * kPackScale > img->height) continue;
        int opaque = 0;
        for (int y = 0; y < kPackScale; ++y)
            for (int x = 0; x < kPackScale; ++x) opaque += (texel(*img, cx * kPackScale + x, cy * kPackScale + y) >> 24) != 0;
        if (opaque == 0 || opaque == kPackScale * kPackScale) continue;
        for (int y = 0; y < kPackScale; ++y)
            for (int x = 0; x < kPackScale; ++x) {
                const uint32_t c = texel(*img, cx * kPackScale + x, cy * kPackScale + y);
                if (c >> 24) out[size_t(y) * kCanvasWidth + x] = c | 0xFF000000u;
            }
    }
}
void Compositor::draw_own(int px, int py, uint32_t* out, uint32_t base, std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of) {
    const size_t i = size_t(py) * 320 + px;
    auto fill = [&](uint32_t c) { for (int y = 0; y < kPackScale; ++y) std::fill_n(out + size_t(y) * kCanvasWidth, kPackScale, c); };
    if (!tag_valid(tags[i])) { fill(base); return; }
    const auto ref = image_of(tags[i]);
    const UpscaledImage* img = ref ? pack_->get(*ref) : nullptr;
    if (!img) { fill(base); return; }
    const auto [sx, sy] = tag_xy(tags[i]);
    if (int(sx + 1) * kPackScale > img->width || int(sy + 1) * kPackScale > img->height) { fill(base); return; }
    // What lies under the transparent parts: the image of a 4-neighbour from another image, continued here.
    const UpscaledImage* under = nullptr;
    int ux = 0, uy = 0;
    uint32_t under_flat = base;
    bool under_found = false;
    static constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, -1, 0, 1};
    for (int k = 0; k < 4 && !under_found; ++k) {
        const int qx = px + kDx[k], qy = py + kDy[k];
        if (qx < 0 || qy < 0 || qx >= 320 || qy >= 200) continue;
        const size_t q = size_t(qy) * 320 + qx;
        if (tag_valid(tags[q]) && tag_image(tags[q]) == tag_image(tags[i])) continue;
        under_found = true;
        under_flat = flat(dac, pixels[q]);
        if (!tag_valid(tags[q])) break;
        const auto qref = image_of(tags[q]);
        const UpscaledImage* qimg = qref ? pack_->get(*qref) : nullptr;
        if (!qimg) break;
        const auto [qsx, qsy] = tag_xy(tags[q]);
        ux = int(qsx) - kDx[k]; uy = int(qsy) - kDy[k];
        if (ux >= 0 && uy >= 0 && (ux + 1) * kPackScale <= qimg->width && (uy + 1) * kPackScale <= qimg->height) under = qimg;
    }
    for (int y = 0; y < kPackScale; ++y)
        for (int x = 0; x < kPackScale; ++x) {
            const uint32_t t = texel(*img, int(sx) * kPackScale + x, int(sy) * kPackScale + y);
            uint32_t c;
            if (t >> 24) c = t | 0xFF000000u;
            else if (under) {
                const uint32_t u = texel(*under, ux * kPackScale + x, uy * kPackScale + y);
                c = (u >> 24) ? (u | 0xFF000000u) : under_flat;
            } else c = under_found ? under_flat : base;
            out[size_t(y) * kCanvasWidth + x] = c;
        }
}
DirtyRect Compositor::update(std::span<const uint8_t> pixels, std::span<const Tag> tags, const Dac& dac, const ImageLookup& image_of) {
    if (pixels.size() != 64000 || tags.size() != 64000) return {};
    if (dac != last_dac_) first_ = true;
    std::vector<uint8_t> dirty(64000, first_ ? 1 : 0);
    if (!first_) {
        for (size_t i = 0; i < 64000; ++i) {
            if (pixels[i] == last_pixels_[i] && tags[i] == last_tags_[i]) continue;
            const int x = int(i % 320), y = int(i / 320);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < 320 && ny < 200) dirty[size_t(ny) * 320 + nx] = 1;
                }
        }
    }
    int x0 = 320, y0 = 200, x1 = -1, y1 = -1;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            if (!dirty[size_t(y) * 320 + x]) continue;
            draw_block(x, y, pixels, tags, dac, image_of);
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
    last_pixels_.assign(pixels.begin(), pixels.end());
    last_tags_.assign(tags.begin(), tags.end());
    last_dac_ = dac;
    first_ = false;
    if (x1 < 0) return {};
    return {x0 * kPackScale, y0 * kPackScale, (x1 - x0 + 1) * kPackScale, (y1 - y0 + 1) * kPackScale};
}
}
