#include "data/rsc_codec.h"
#include <array>
namespace mtv {
namespace {
constexpr int kEscape = 0x100, kFirstNode = 0x101;
struct Reader {
    std::span<const uint8_t> d; size_t pos; int left = 0; uint8_t cur = 0;
    bool byte(uint8_t& v) { if (pos >= d.size()) return false; v = d[pos++]; return true; }
    bool bit(int& b) { // LSB first, as FUN_47b4_013d
        if (left == 0) { if (!byte(cur)) return false; left = 7; b = cur & 1; return true; }
        cur >>= 1; --left; b = cur & 1; return true;
    }
};
struct Trees {
    std::vector<int> left, right; int next = kFirstNode; size_t capacity;
    explicit Trees(size_t n) : left(n, 0), right(n, 0), capacity(n) {}
};
// FUN_47b4_0166: 0 = internal node (left subtree, right subtree), 1 = leaf; the first leaf of each tree is the escape.
bool read_tree(Reader& r, Trees& t, int& node, bool& escape_used, int depth) {
    if (depth > 600) return false;
    int b;
    if (!r.bit(b)) return false;
    if (b == 0) {
        node = t.next++;
        size_t i = size_t(node - kFirstNode);
        if (i >= t.capacity) return false;
        int l, rr;
        if (!read_tree(r, t, l, escape_used, depth + 1) || !read_tree(r, t, rr, escape_used, depth + 1)) return false;
        t.left[i] = l; t.right[i] = rr;
        return true;
    }
    if (!escape_used) { escape_used = true; node = kEscape; return true; }
    int v = 0;
    for (int k = 0; k < 8; ++k) { int x; if (!r.bit(x)) return false; v = (x << 7) | (v >> 1); }
    node = v;
    return true;
}
bool walk(Reader& r, const Trees& t, int node, int& out) {
    while (node > kEscape) {
        int b; if (!r.bit(b)) return false;
        size_t i = size_t(node - kFirstNode);
        node = b ? t.right[i] : t.left[i];
    }
    out = node;
    return true;
}
bool raw8(Reader& r, int& v) {
    v = 0;
    for (int k = 0; k < 8; ++k) { int x; if (!r.bit(x)) return false; v = (x << 7) | (v >> 1); }
    return true;
}
}
Result<ChunkyImage> decode_rsc_image(std::span<const uint8_t> e) {
    auto fail = [](size_t at, const char* why) -> Result<ChunkyImage> { return DataError{"rsc", "image", at, why}; };
    if (e.empty()) return fail(0, "empty entry");
    const size_t n = e[0];
    if (n == 0 || e.size() < 1 + 2 * n + 6) return fail(0, "header truncated");
    ChunkyImage img;
    uint64_t pixels_total = 0;
    for (size_t i = 0; i < n; ++i) {
        ChunkyFrame f;
        f.width = e[1 + 2 * i]; f.height = e[2 + 2 * i];
        f.stride = uint16_t(((f.width + 7) / 8) * 8);
        f.pixels.assign(size_t(f.stride) * f.height, 0);
        pixels_total += uint64_t(f.width) * f.height;
        img.frames.push_back(std::move(f));
    }
    Reader r{e, 1 + 2 * n};
    const uint16_t nodes = uint16_t(e[r.pos] << 8 | e[r.pos + 1]);
    const uint32_t count = uint32_t(e[r.pos + 2]) << 24 | uint32_t(e[r.pos + 3]) << 16 | uint32_t(e[r.pos + 4]) << 8 | e[r.pos + 5];
    r.pos += 6;
    if (count != pixels_total) return fail(1 + 2 * n + 2, "pixel count does not match the frame table");
    Trees t(nodes);
    int root; bool esc = false;
    if (!read_tree(r, t, root, esc, 0)) return fail(r.pos, "root tree invalid or truncated");
    std::array<int, 256> ctx{};
    for (auto& c : ctx) { esc = false; if (!read_tree(r, t, c, esc, 0)) return fail(r.pos, "context tree invalid or truncated"); }
    size_t frame = 0, x = 0, y = 0;
    auto emit = [&](uint8_t v) {
        while (img.frames[frame].width == 0 || img.frames[frame].height == 0) ++frame;
        auto& f = img.frames[frame];
        f.pixels[y * f.stride + x] = v;
        if (++x == f.width) { x = 0; if (++y == f.height) { y = 0; ++frame; } }
    };
    if (count == 0) return img;
    // FUN_47b4_01fe: start with the root tree; afterwards use the context tree of the previous byte.
    // An escape in a context tree retries with the root tree; an escape there reads a raw literal.
    int sym;
    if (!walk(r, t, root, sym)) return fail(r.pos, "stream truncated");
    if (sym == kEscape && !raw8(r, sym)) return fail(r.pos, "stream truncated");
    for (uint32_t done = 0;;) {
        emit(uint8_t(sym));
        if (++done == count) break;
        int next;
        if (!walk(r, t, ctx[size_t(sym)], next)) return fail(r.pos, "stream truncated");
        if (next == kEscape) {
            if (!walk(r, t, root, next)) return fail(r.pos, "stream truncated");
            if (next == kEscape && !raw8(r, next)) return fail(r.pos, "stream truncated");
        }
        sym = next;
    }
    return img;
}
Result<std::vector<uint8_t>> decode_rsc_lzw(std::span<const uint8_t> e) {
    auto fail = [](size_t at, const char* why) -> Result<std::vector<uint8_t>> { return DataError{"rsc", "lzw", at, why}; };
    if (e.size() < 8 || e[4] != 0 || e[5] != 8 || e[6] != 9 || e[7] != 0) return fail(0, "not an LZW entry");
    const uint16_t unpacked = uint16_t(e[0] | e[1] << 8);
    int remaining = int(e[2] | e[3] << 8);
    if (size_t(remaining) > e.size() - 8) return fail(2, "packed size larger than the entry");
    // Port of FUN_46f4_015e. Bytes past the entry read as 0; they only feed the final, discarded code.
    static constexpr uint16_t mask[17] = {0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0, 0, 0, 0, 0, 0, 0xFFFF, 0xFFFF, 0xFFFF};
    size_t p = 8; int bitpos = 0, width = 9;
    auto at = [&](size_t i) -> unsigned { return i < e.size() ? e[i] : 0u; };
    auto get = [&]() -> unsigned {
        int local = width + bitpos;
        unsigned code = ((mask[bitpos] & at(p)) << (local - 8)) & 0xFFFF;
        const size_t p0 = p++;
        int rem = remaining - 1;
        if (local > 16) {
            bitpos -= 8; local -= 8;
            code |= (at(p) << (local - 8)) & 0xFFFF;
            p = p0 + 2; rem = remaining - 2;
        }
        remaining = rem;
        code |= (at(p) >> (16 - local)) & mask[16 - local];
        bitpos += width - 8;
        if (bitpos > 7) { bitpos -= 8; ++p; --remaining; }
        return code;
    };
    struct Node { uint16_t prev, first, last; };
    std::vector<Node> dict(0x102);
    for (uint16_t i = 0; i < 256; ++i) dict[i] = {0xFFFF, i, i};
    std::vector<uint8_t> out, stack;
    auto emit = [&](unsigned code) -> bool {
        stack.clear();
        while (code != 0xFFFF) {
            if (code >= dict.size() || stack.size() > dict.size()) return false;
            stack.push_back(uint8_t(dict[code].last));
            code = dict[code].prev;
        }
        out.insert(out.end(), stack.rbegin(), stack.rend());
        return out.size() <= unpacked;
    };
    unsigned next = 0x102, code = get(), old = code;
    if (code >= 0x100 || !emit(code)) return fail(8, "invalid first code");
    code = get();
    while (remaining > 0) {
        if (code == 0x100) {
            width = 9; next = 0x102; dict.resize(0x102);
            code = get(); old = code;
            if (code >= 0x100 || !emit(code)) return fail(p, "invalid code after reset");
        } else if (code == 0x101) {
            if (++width > 16) return fail(p, "code width above 16 bits");
        } else {
            if (code > next || old >= dict.size()) return fail(p, "undefined code");
            if (dict.size() <= next) dict.resize(next + 1);
            dict[next] = {uint16_t(old), dict[old].first, 0};
            dict[next].last = dict[code].first;
            ++next; old = code;
            if (!emit(code)) return fail(p, "output larger than the declared size");
        }
        code = get();
    }
    if (out.size() != unpacked) return fail(0, "output size differs from the declared size");
    return out;
}
Result<DecodedEntry> decode_rsc_entry(const ResourceIndex& index, uint16_t slot) {
    auto raw = index.get(slot);
    if (!raw.has_value()) return raw.error();
    DecodedEntry d;
    d.bytes.assign(raw.value().begin(), raw.value().end());
    auto img = decode_rsc_image(raw.value());
    if (img.has_value()) { d.kind = ResourceKind::Image; d.image = std::move(img.value()); return d; }
    auto lzw = decode_rsc_lzw(raw.value());
    if (lzw.has_value()) {
        const auto& v = lzw.value();
        d.kind = v.size() >= 3 && v[0] == 'P' && v[1] == 'L' && v[2] == 'X' ? ResourceKind::Music : ResourceKind::Animation;
        d.bytes = std::move(lzw.value());
    }
    return d;
}
}
