#pragma once
// Names the pixels Mad TV's image decoder writes: at the decoder's entry (FUN_47b4_04ac, image offset 0x37FEC)
// the packed input identifies the slot; at its pixel store (0x37C36) the decoder's own counters give the frame
// (DS:A684 - 1), the column (DS:A672) and the row (DS:A64C).
#include "render/provenance.h"
#include "rt/cpu.h"
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>
namespace mtv::game {
class ImageProvenance {
public:
    static constexpr uint32_t kDecoderEntry = 0x37FEC, kPixelStore = 0x37C36;
    explicit ImageProvenance(const std::filesystem::path& game_dir);
    void decoder_entry(const rt::Cpu& c);
    render::Tag pixel_tag(const rt::Cpu& c);
    std::optional<render::ImageRef> image_of(render::Tag t) const;
    // The slot whose packed entry starts at `p` (`available` bytes readable there).
    std::optional<uint16_t> slot_of(const uint8_t* p, size_t available) const;
private:
    std::vector<size_t> lengths_; // distinct key lengths, longest first
    static uint64_t key(const uint8_t* p, size_t n);
    std::unordered_map<uint64_t, uint16_t> slot_by_prefix_;
    std::optional<uint16_t> slot_;
    std::map<std::pair<uint16_t, uint16_t>, uint32_t> ids_;
    std::vector<render::ImageRef> refs_{render::ImageRef{}}; // id 0 is unused
};
}
