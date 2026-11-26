#include "rt/cpu.h"
#include "rt/machine.h"
#include <cstdio>
#include <cstdlib>
namespace mtv::rt {
void mul8(Cpu& c, uint8_t v) { uint16_t r = uint16_t(uint8_t(c.r[AX]) * v); c.s16(AX, r); c.cf = c.of = (r >> 8) != 0; szp<8>(c, r); c.zf = false; }
void imul8(Cpu& c, uint8_t v) { int16_t r = int16_t(int8_t(c.r[AX]) * int8_t(v)); c.s16(AX, uint16_t(r)); c.cf = c.of = r != int8_t(r); }
void mul16(Cpu& c, uint16_t v) { uint32_t r = uint32_t(c.r[AX]) * v; c.s16(AX, uint16_t(r)); c.s16(DX, uint16_t(r >> 16)); c.cf = c.of = (r >> 16) != 0; szp<16>(c, r); c.zf = false; }
void imul16(Cpu& c, uint16_t v) { int32_t r = int32_t(int16_t(c.r[AX])) * int16_t(v); c.s16(AX, uint16_t(r)); c.s16(DX, uint16_t(uint32_t(r) >> 16)); c.cf = c.of = r != int16_t(r); }
uint16_t imul16_3(Cpu& c, uint16_t a, uint16_t b) { int32_t r = int32_t(int16_t(a)) * int16_t(b); c.cf = c.of = r != int16_t(r); return uint16_t(r); }
bool div8(Cpu& c, uint8_t v) {
    if (!v) return false; uint16_t a = c.r[AX]; uint16_t q = a / v; if (q > 0xFF) return false;
    c.s16(AX, uint16_t(((a % v) << 8) | q)); return true;
}
bool idiv8(Cpu& c, uint8_t v) {
    if (!v) return false; int16_t a = int16_t(c.r[AX]); int16_t q = int16_t(a / int8_t(v)); if (q > 127 || q < -128) return false;
    c.s16(AX, uint16_t((uint8_t(a % int8_t(v)) << 8) | uint8_t(q))); return true;
}
bool div16(Cpu& c, uint16_t v) {
    if (!v) return false; uint32_t a = uint32_t(c.r[DX]) << 16 | c.r[AX]; uint32_t q = a / v; if (q > 0xFFFF) return false;
    c.s16(AX, uint16_t(q)); c.s16(DX, uint16_t(a % v)); return true;
}
bool idiv16(Cpu& c, uint16_t v) {
    if (!v) return false; int32_t a = int32_t(uint32_t(c.r[DX]) << 16 | c.r[AX]); int32_t q = a / int16_t(v);
    if (q > 32767 || q < -32768) return false;
    c.s16(AX, uint16_t(q)); c.s16(DX, uint16_t(a % int16_t(v))); return true;
}
void daa(Cpu& c) {
    uint8_t al = uint8_t(c.r[AX]); bool cf = c.cf; uint8_t old = al;
    if ((al & 0xF) > 9 || c.af) { al = uint8_t(al + 6); c.af = true; } else c.af = false;
    if (old > 0x99 || cf) { al = uint8_t(al + 0x60); c.cf = true; } else c.cf = false;
    c.s8(AL, al); szp<8>(c, al);
}
void das(Cpu& c) {
    uint8_t al = uint8_t(c.r[AX]); bool cf = c.cf; uint8_t old = al;
    if ((al & 0xF) > 9 || c.af) { al = uint8_t(al - 6); c.af = true; } else c.af = false;
    if (old > 0x99 || cf) { al = uint8_t(al - 0x60); c.cf = true; } else c.cf = false;
    c.s8(AL, al); szp<8>(c, al);
}
void aaa(Cpu& c) {
    if ((c.r[AX] & 0xF) > 9 || c.af) { c.r[AX] = uint16_t(c.r[AX] + 0x106); c.af = c.cf = true; } else c.af = c.cf = false;
    c.s8(AL, uint8_t(c.r[AX] & 0xF));
}
void aas(Cpu& c) {
    if ((c.r[AX] & 0xF) > 9 || c.af) { c.r[AX] = uint16_t(c.r[AX] - 6); c.s8(AH, uint8_t((c.r[AX] >> 8) - 1)); c.af = c.cf = true; } else c.af = c.cf = false;
    c.s8(AL, uint8_t(c.r[AX] & 0xF));
}
void aam(Cpu& c, uint8_t base) { uint8_t al = uint8_t(c.r[AX]); c.s16(AX, uint16_t(((al / base) << 8) | (al % base))); szp<8>(c, al % base); }
void aad(Cpu& c, uint8_t base) { uint8_t r = uint8_t((c.r[AX] & 0xFF) + (c.r[AX] >> 8) * base); c.s16(AX, r); szp<8>(c, r); }
static int step(const Cpu& c, int n) { return c.df ? -n : n; }
void movs8(Cpu& c, uint8_t seg) { uint32_t a = c.lin(seg, c.r[SI]); c.m->w8(c.lin(ES, c.r[DI]), c.m->r8(a), c.m->tag[a]); c.r[SI] = uint16_t(c.r[SI] + step(c, 1)); c.r[DI] = uint16_t(c.r[DI] + step(c, 1)); }
void movs16(Cpu& c, uint8_t seg) {
    uint32_t a = c.lin(seg, c.r[SI]), d = c.lin(ES, c.r[DI]);
    c.m->w16(d, c.m->r16(a), c.m->tag[a], c.m->tag[(a + 1) & 0xFFFFF]);
    c.r[SI] = uint16_t(c.r[SI] + step(c, 2)); c.r[DI] = uint16_t(c.r[DI] + step(c, 2));
}
void stos8(Cpu& c) { c.st8(c.lin(ES, c.r[DI]), AL); c.r[DI] = uint16_t(c.r[DI] + step(c, 1)); }
void stos16(Cpu& c) { c.st16(c.lin(ES, c.r[DI]), AX); c.r[DI] = uint16_t(c.r[DI] + step(c, 2)); }
void lods8(Cpu& c, uint8_t seg) { c.ld8(AL, c.lin(seg, c.r[SI])); c.r[SI] = uint16_t(c.r[SI] + step(c, 1)); }
void lods16(Cpu& c, uint8_t seg) { c.ld16(AX, c.lin(seg, c.r[SI])); c.r[SI] = uint16_t(c.r[SI] + step(c, 2)); }
void scas8(Cpu& c) { sub<8>(c, uint8_t(c.r[AX]), c.m->r8(c.lin(ES, c.r[DI]))); c.r[DI] = uint16_t(c.r[DI] + step(c, 1)); }
void scas16(Cpu& c) { sub<16>(c, c.r[AX], c.m->r16(c.lin(ES, c.r[DI]))); c.r[DI] = uint16_t(c.r[DI] + step(c, 2)); }
void cmps8(Cpu& c, uint8_t seg) { sub<8>(c, c.m->r8(c.lin(seg, c.r[SI])), c.m->r8(c.lin(ES, c.r[DI]))); c.r[SI] = uint16_t(c.r[SI] + step(c, 1)); c.r[DI] = uint16_t(c.r[DI] + step(c, 1)); }
void cmps16(Cpu& c, uint8_t seg) { sub<16>(c, c.m->r16(c.lin(seg, c.r[SI])), c.m->r16(c.lin(ES, c.r[DI]))); c.r[SI] = uint16_t(c.r[SI] + step(c, 2)); c.r[DI] = uint16_t(c.r[DI] + step(c, 2)); }
void ivt_write(uint32_t a, uint32_t last_entry) { char b[96]; std::snprintf(b, sizeof b, "write to interrupt vector table at %05X, last function entered %05X", a, last_entry); throw std::runtime_error(b); }
bool g_trace_entries = std::getenv("MTV_TRACE_ENTRIES") != nullptr || std::getenv("MTV_WATCH_IVT") != nullptr || std::getenv("MTV_SNAP") != nullptr;
// MTV_SNAP=<image offset hex>[@<cs hex>][:<occurrence>] writes registers and memory at that function entry to
// MTV_SNAP_FILE; MTV_SNAP_AFTER=<instruction count> skips earlier entries.
static void snapshot(const Cpu& c, uint32_t off) {
    static const char* spec = std::getenv("MTV_SNAP");
    static int seen = 0;
    if (!spec) return;
    unsigned want = 0, occ = 1, cs_want = 0;
    if (std::sscanf(spec, "%x@%x:%u", &want, &cs_want, &occ) < 2) { cs_want = 0; std::sscanf(spec, "%x:%u", &want, &occ); }
    static const unsigned long long after = std::getenv("MTV_SNAP_AFTER") ? std::strtoull(std::getenv("MTV_SNAP_AFTER"), nullptr, 10) : 0;
    if (off != want || (cs_want && c.s[CS] != cs_want) || c.icount < after || ++seen != int(occ)) return;
    std::FILE* f = std::fopen(std::getenv("MTV_SNAP_FILE") ? std::getenv("MTV_SNAP_FILE") : "snap.bin", "wb");
    uint16_t regs[13];
    for (int i = 0; i < 8; ++i) regs[i] = c.r[size_t(i)];
    for (int i = 0; i < 4; ++i) regs[8 + i] = c.s[size_t(i)];
    regs[12] = c.flags();
    std::fwrite(regs, 2, 13, f);
    std::fwrite(c.m->b.data(), 1, 0x100000, f);
    std::fclose(f);
}
void trace_entry(const Cpu& c, uint32_t off) {
    c.m->last_entry = off; snapshot(c, off);
    static const char* on = std::getenv("MTV_TRACE_ENTRIES");
    static const unsigned long long from = std::getenv("MTV_TRACE_FROM") ? std::strtoull(std::getenv("MTV_TRACE_FROM"), nullptr, 10) : 0;
    if (!on || c.icount < from) return;
    std::fprintf(stderr, "enter %05X CS=%04X SP=%04X at %llu\n", off, c.s[CS], c.r[SP], (unsigned long long)c.icount); }
[[noreturn]] void fatal(const Cpu& c, uint32_t at, const std::string& why) {
    throw std::runtime_error("translated code at image offset 0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%X", at); return std::string(b); }() +
                             ": " + why + " (CS=" + std::to_string(c.s[CS]) + ")");
}
}
