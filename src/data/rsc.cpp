#include "data/rsc.h"
#include <algorithm>
#include <fstream>
#include <iterator>
namespace mtv {
Result<RscArchive> RscArchive::open(std::vector<uint8_t> bytes, std::string name) {
    RscArchive a;
    a.name_ = std::move(name);
    a.index_.fill(-1);
    if (bytes.size() < kRscTableBytes) return DataError{a.name_, "table", bytes.size(), "file shorter than the 600-slot table"};
    for (uint16_t slot = 0; slot < kRscSlots; ++slot) {
        const uint8_t* e = bytes.data() + slot * 6;
        uint32_t off = uint32_t(e[0]) | uint32_t(e[1]) << 8 | uint32_t(e[2]) << 16 | uint32_t(e[3]) << 24;
        uint16_t size = uint16_t(e[4] | e[5] << 8);
        if (off == 0) continue;
        std::string res = "slot " + std::to_string(slot);
        if (off < kRscTableBytes) return DataError{a.name_, res, slot * 6u, "entry starts inside the table"};
        if (off > bytes.size() || size > bytes.size() - off) return DataError{a.name_, res, off, "entry extends past end of file"};
        a.index_[slot] = int16_t(a.entries_.size());
        a.entries_.push_back({slot, off, size});
    }
    auto sorted = a.entries_;
    std::sort(sorted.begin(), sorted.end(), [](auto& x, auto& y) { return x.offset < y.offset; });
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i - 1].offset + sorted[i - 1].size > sorted[i].offset)
            return DataError{a.name_, "slot " + std::to_string(sorted[i].slot), sorted[i].offset, "entry overlaps slot " + std::to_string(sorted[i - 1].slot)};
    a.bytes_ = std::move(bytes);
    return a;
}
Result<std::span<const uint8_t>> RscArchive::get(uint16_t slot) const {
    if (slot >= kRscSlots || index_[slot] < 0) return DataError{name_, "slot " + std::to_string(slot), 0, "slot not present"};
    const auto& e = entries_[size_t(index_[slot])];
    return std::span<const uint8_t>(bytes_).subspan(e.offset, e.size);
}
Result<void> ResourceIndex::add(RscArchive archive) {
    for (const auto& e : archive.entries())
        if (owner_[e.slot] >= 0)
            return DataError{archive.name(), "slot " + std::to_string(e.slot), 0, "slot already provided by " + archives_[size_t(owner_[e.slot])].name()};
    for (const auto& e : archive.entries()) owner_[e.slot] = int8_t(archives_.size());
    archives_.push_back(std::move(archive));
    return {};
}
Result<std::span<const uint8_t>> ResourceIndex::get(uint16_t slot) const {
    if (slot >= kRscSlots || owner_[slot] < 0) return DataError{"*.rsc", "slot " + std::to_string(slot), 0, "slot not present"};
    return archives_[size_t(owner_[slot])].get(slot);
}
std::string ResourceIndex::archive_of(uint16_t slot) const {
    return slot < kRscSlots && owner_[slot] >= 0 ? archives_[size_t(owner_[slot])].name() : std::string{};
}
std::vector<uint16_t> ResourceIndex::slots() const {
    std::vector<uint16_t> out;
    for (uint16_t s = 0; s < kRscSlots; ++s) if (owner_[s] >= 0) out.push_back(s);
    return out;
}
Result<ResourceIndex> ResourceIndex::open_dir(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        auto ext = f.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (f.is_regular_file() && ext == ".rsc") files.push_back(f.path());
    }
    if (ec) return DataError{dir.string(), "", 0, "cannot list directory"};
    if (files.empty()) return DataError{dir.string(), "", 0, "no .rsc files"};
    std::sort(files.begin(), files.end());
    ResourceIndex idx;
    for (const auto& p : files) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return DataError{p.string(), "", 0, "cannot open"};
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
        auto a = RscArchive::open(std::move(bytes), p.filename().string());
        if (!a.has_value()) return a.error();
        auto r = idx.add(std::move(a.value()));
        if (!r.has_value()) return r.error();
    }
    return idx;
}
}
