#pragma once
#include "data/result.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace mtv {
inline constexpr uint16_t kRscSlots = 600;
inline constexpr uint32_t kRscTableBytes = kRscSlots * 6; // 0xE10
struct RscEntry { uint16_t slot; uint32_t offset; uint16_t size; };
// One SOURCEn.RSC file: a 600-slot table (offset low word, offset high word, size) then payloads.
// A slot is used when its offset is non-zero (original loader 0x48640).
class RscArchive {
public:
    static Result<RscArchive> open(std::vector<uint8_t> bytes, std::string name);
    std::span<const RscEntry> entries() const { return entries_; }
    Result<std::span<const uint8_t>> get(uint16_t slot) const;
    const std::string& name() const { return name_; }
private:
    std::vector<uint8_t> bytes_;
    std::string name_;
    std::vector<RscEntry> entries_;
    std::array<int16_t, kRscSlots> index_{};
};
// All *.rsc files merged into one global slot space, as the game does. Slots must not repeat across files.
class ResourceIndex {
public:
    static Result<ResourceIndex> open_dir(const std::filesystem::path& dir);
    Result<void> add(RscArchive archive);
    Result<std::span<const uint8_t>> get(uint16_t slot) const;
    std::string archive_of(uint16_t slot) const;
    std::vector<uint16_t> slots() const;
private:
    std::vector<RscArchive> archives_;
    std::array<int8_t, kRscSlots> owner_ = [] { std::array<int8_t, kRscSlots> a{}; a.fill(-1); return a; }();
};
}
