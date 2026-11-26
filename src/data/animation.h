#pragma once
#include "data/image.h"
#include "data/result.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace mtv {
// Animated sequences: the table at DS:161E (FUN_465f_00a5) pairs a backdrop image slot with up to three LZW
// delta slots, played in order by FUN_465f_0244.
struct AnimationSequence { uint16_t backdrop; std::array<uint16_t, 3> parts; };
inline constexpr std::array<AnimationSequence, 14> kAnimationSequences{{
    {305, {302, 303, 304}}, {301, {300, 0, 0}}, {310, {311, 312, 0}}, {314, {315, 0, 0}}, {316, {317, 0, 0}},
    {318, {319, 0, 0}}, {320, {321, 0, 0}}, {320, {322, 0, 0}}, {323, {324, 0, 0}}, {325, {326, 0, 0}},
    {327, {328, 0, 0}}, {329, {330, 331, 332}}, {333, {334, 335, 0}}, {336, {337, 0, 0}}}};
// Applies the delta frames (FUN_5456_0000 at 0x54560) to a copy of `backdrop`. Byte 0 is the frame count;
// each frame starts at pixel 0 of a buffer whose rows are padded to a multiple of 8 and ends with 0x62.
// 0x63 = run (length bytes summed while 0xFF, then 0x64 = skip or a fill colour), 0x64 = skip one, other = literal.
Result<std::vector<IndexedImage>> decode_animation(std::span<const uint8_t> data, const IndexedImage& backdrop);
}
