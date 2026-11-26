#pragma once
#include "data/result.h"
#include "data/rsc.h"
#include <cstdint>
#include <span>
#include <vector>
namespace mtv {
enum class ResourceKind { Palette, Image, Font, Text, Music, Sound, Table, Driver, Animation, Unknown };
// One frame as the game holds it after unpacking: one byte per pixel (colour index), rows padded to a multiple of 8.
struct ChunkyFrame { uint16_t width = 0, height = 0, stride = 0; std::vector<uint8_t> pixels; };
struct ChunkyImage { std::vector<ChunkyFrame> frames; };
// Packed image entries (loaded by FUN_1d5e_011b / FUN_1d5e_01a1, decoded by FUN_47b4_04ac at 0x47FEC):
// order-1 Huffman with one root tree, 256 context trees and an escape to raw 8-bit literals.
Result<ChunkyImage> decode_rsc_image(std::span<const uint8_t> entry);
// Music entries (FUN_46f4_0009 at 0x46F49): u16 unpacked size, u16 packed size, 4 unused bytes, then LZW codes
// read MSB first, starting 9 bits wide; 0x100 resets the dictionary, 0x101 widens codes by one bit. The decoder
// stops when the packed byte count runs out, discarding the code read last.
Result<std::vector<uint8_t>> decode_rsc_lzw(std::span<const uint8_t> entry);
struct DecodedEntry { ResourceKind kind = ResourceKind::Unknown; std::vector<uint8_t> bytes; ChunkyImage image; };
Result<DecodedEntry> decode_rsc_entry(const ResourceIndex& index, uint16_t slot);
}
