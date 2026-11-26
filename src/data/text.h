#pragma once
#include "data/result.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace mtv {
enum class Language : uint8_t { German = 0, English = 1 };
// Layouts of the text slots (see analysis/findings.md, Task 9).
enum class TextLayout {
    OffsetsNul,     // u16 offset table (first offset / 2 = count), NUL-terminated strings
    OffsetsRaw,     // u16 offset table, each span is the whole string
    OffsetsRecords, // u16 offset table, each record starts with a length-prefixed string (more fields follow)
    Sequence,       // block_entries NUL-terminated strings from offset 0 (430 messages, FUN_1054_0a0b)
    LengthBlocks,   // repeated [u16 length table of N entries][strings concatenated in table order]
};
struct TextTable { std::vector<std::string> utf8; };
Result<TextTable> decode_text(std::span<const uint8_t> entry, TextLayout layout, int block_entries = 0);
struct TextLayoutSpec { TextLayout layout; int block_entries = 0; };
std::optional<TextLayoutSpec> text_layout_for_slot(uint16_t slot);
// German slot <-> English slot, from the language table at DS:055E. Other slots map to themselves.
uint16_t language_slot(uint16_t slot, Language language);
// Code page 437 as used by the game. Glyph-only bytes 0x01-0x07 and 0xC0-0xC5 map to U+E000 + byte.
std::string cp437_to_utf8(std::string_view cp437);
std::string utf8_to_cp437(std::string_view utf8);
}
