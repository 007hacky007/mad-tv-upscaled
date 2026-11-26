#include "data/text.h"
#include <array>
namespace mtv {
namespace {
constexpr std::array<std::pair<uint8_t, char32_t>, 21> kCp437{{
    {0x81, U'ü'}, {0x82, U'é'}, {0x83, U'â'}, {0x84, U'ä'}, {0x85, U'à'}, {0x88, U'ê'}, {0x8A, U'è'}, {0x8E, U'Ä'},
    {0x90, U'É'}, {0x94, U'ö'}, {0x97, U'ù'}, {0x99, U'Ö'}, {0x9A, U'Ü'}, {0xA0, U'á'}, {0xA1, U'í'}, {0xA2, U'ó'},
    {0xA3, U'ú'}, {0xA4, U'ñ'}, {0xA8, U'¿'}, {0xAD, U'¡'}, {0xE1, U'ß'}}};
constexpr std::array<std::pair<uint16_t, uint16_t>, 8> kPairs{{{199, 253}, {42, 246}, {207, 252}, {47, 251}, {45, 250}, {46, 249}, {49, 248}, {44, 247}}};
void put_utf8(std::string& s, char32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
    else { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
}
DataError err(size_t at, std::string why) { return DataError{"rsc", "text", at, std::move(why)}; }
uint16_t u16(std::span<const uint8_t> e, size_t at) { return uint16_t(e[at] | e[at + 1] << 8); }
}
std::string cp437_to_utf8(std::string_view in) {
    std::string out;
    for (unsigned char c : in) {
        if (c < 0x80 && (c >= 0x20 || c == 0 || c == '\n') ) { out += char(c); continue; }
        char32_t u = 0;
        for (auto [b, cp] : kCp437) if (b == c) u = cp;
        if (!u) u = 0xE000 + c; // game-specific glyph
        put_utf8(out, u);
    }
    return out;
}
std::string utf8_to_cp437(std::string_view in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        unsigned char c = uint8_t(in[i]);
        char32_t u; size_t n;
        if (c < 0x80) { u = c; n = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) { u = char32_t(c & 0x1F) << 6 | (uint8_t(in[i + 1]) & 0x3F); n = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < in.size()) { u = char32_t(c & 0x0F) << 12 | char32_t(uint8_t(in[i + 1]) & 0x3F) << 6 | (uint8_t(in[i + 2]) & 0x3F); n = 3; }
        else { out += '.'; ++i; continue; }
        i += n;
        if (u < 0x80) { out += char(u); continue; }
        if (u >= 0xE000 && u <= 0xE0FF) { out += char(u - 0xE000); continue; }
        char b = '.';
        for (auto [code, cp] : kCp437) if (cp == u) b = char(code);
        out += b;
    }
    return out;
}
uint16_t language_slot(uint16_t slot, Language lang) {
    for (auto [de, en] : kPairs) if (slot == de || slot == en) return lang == Language::German ? de : en;
    return slot;
}
std::optional<TextLayoutSpec> text_layout_for_slot(uint16_t slot) {
    switch (language_slot(slot, Language::German)) {
    case 199: return TextLayoutSpec{TextLayout::Sequence, 430};
    case 42: return TextLayoutSpec{TextLayout::OffsetsNul};
    case 207: case 47: case 45: return TextLayoutSpec{TextLayout::OffsetsRecords};
    case 49: return TextLayoutSpec{TextLayout::OffsetsRaw};
    case 46: return TextLayoutSpec{TextLayout::LengthBlocks, 51};
    case 44: return TextLayoutSpec{TextLayout::LengthBlocks, 561};
    default: return std::nullopt;
    }
}
Result<TextTable> decode_text(std::span<const uint8_t> e, TextLayout layout, int block_entries) {
    TextTable t;
    auto add = [&](std::span<const uint8_t> s) { t.utf8.push_back(cp437_to_utf8(std::string_view(reinterpret_cast<const char*>(s.data()), s.size()))); };
    switch (layout) {
    case TextLayout::OffsetsNul: case TextLayout::OffsetsRaw: case TextLayout::OffsetsRecords: {
        if (e.size() < 2) return err(0, "missing offset table");
        const size_t n = u16(e, 0) / 2;
        if (n == 0 || n * 2 > e.size()) return err(0, "offset table larger than the entry");
        std::vector<size_t> offs(n + 1, e.size());
        for (size_t i = 0; i < n; ++i) offs[i] = u16(e, i * 2);
        for (size_t i = 0; i < n; ++i) {
            if (offs[i] < n * 2 || offs[i] > offs[i + 1]) return err(i * 2, "offset out of order or range");
            auto span = e.subspan(offs[i], offs[i + 1] - offs[i]);
            if (layout == TextLayout::OffsetsNul) {
                size_t z = 0; while (z < span.size() && span[z]) ++z;
                add(span.first(z));
            } else if (layout == TextLayout::OffsetsRaw) add(span);
            else if (span.empty()) add(span);
            else {
                if (size_t(span[0]) + 1 > span.size()) return err(offs[i], "record string longer than the record");
                add(span.subspan(1, span[0]));
            }
        }
        return t;
    }
    case TextLayout::Sequence: {
        if (block_entries <= 0) return err(0, "string count required");
        size_t p = 0;
        while (t.utf8.size() < size_t(block_entries) && p < e.size()) {
            size_t z = p; while (z < e.size() && e[z]) ++z;
            if (z == e.size()) return err(p, "string not terminated");
            add(e.subspan(p, z - p));
            p = z + 1;
        }
        if (t.utf8.size() != size_t(block_entries)) return err(p, "fewer strings than expected");
        return t;
    }
    case TextLayout::LengthBlocks: {
        if (block_entries <= 0) return err(0, "block size required");
        size_t p = 0;
        while (p < e.size()) {
            if (p + size_t(block_entries) * 2 > e.size()) return err(p, "length table truncated");
            std::vector<uint16_t> lens(static_cast<size_t>(block_entries));
            for (size_t i = 0; i < lens.size(); ++i) lens[i] = u16(e, p + i * 2);
            p += lens.size() * 2;
            for (auto l : lens) {
                if (p + l > e.size()) return err(p, "string past end of entry");
                add(e.subspan(p, l));
                p += l;
            }
        }
        return t;
    }
    }
    return err(0, "unknown layout");
}
}
