#include "data/mz.h"
#include "data/byte_reader.h"
namespace mtv {
Result<MzImage> parse_mz(std::span<const uint8_t> file) {
    ByteReader r(std::as_bytes(file), "mz");
    auto sig = r.u16le();
    if (!sig.has_value() || sig.value() != 0x5A4D) return DataError{"mz", "", 0, "missing MZ signature"};
    MzImage m;
    uint16_t* fields[] = {&m.header.last_page_bytes, &m.header.pages, &m.header.relocation_count, &m.header.header_paragraphs,
                          &m.header.minalloc, &m.header.maxalloc, &m.header.ss, &m.header.sp, &m.header.checksum,
                          &m.header.ip, &m.header.cs, &m.header.relocation_offset, &m.header.overlay};
    for (auto* f : fields) { auto v = r.u16le(); if (!v.has_value()) return v.error(); *f = v.value(); }
    size_t file_size = m.header.pages == 0 ? 0 : size_t(m.header.pages - 1) * 512 + (m.header.last_page_bytes ? m.header.last_page_bytes : 512);
    if (file_size > file.size()) return DataError{"mz", "", file.size(), "file shorter than MZ size fields"};
    size_t header_bytes = size_t(m.header.header_paragraphs) * 16;
    if (header_bytes > file_size) return DataError{"mz", "", header_bytes, "header larger than file"};
    ByteReader rel(std::as_bytes(file.first(header_bytes)), "mz");
    if (!rel.take(m.header.relocation_offset).has_value()) return DataError{"mz", "", m.header.relocation_offset, "relocation table outside header"};
    for (uint16_t i = 0; i < m.header.relocation_count; ++i) {
        auto off = rel.u16le(); auto seg = rel.u16le();
        if (!off.has_value() || !seg.has_value()) return rel.error("relocation table outside header");
        m.relocations.emplace_back(off.value(), seg.value());
    }
    m.image.assign(file.begin() + header_bytes, file.begin() + file_size);
    return m;
}
}
