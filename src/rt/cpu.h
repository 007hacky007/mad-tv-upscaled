#pragma once
// Runtime for code translated instruction by instruction from the original 16-bit x86 program
// (tools/recomp). Memory is the 1 MiB real-mode address space; every byte carries a 32-bit tag
// naming the picture pixel it came from (0 = none), so the upscaler knows what each screen pixel is.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
namespace mtv::rt {
enum Reg16 : uint8_t { AX, CX, DX, BX, SP, BP, SI, DI };
enum Reg8 : uint8_t { AL, CL, DL, BL, AH, CH, DH, BH };
enum Seg : uint8_t { ES, CS, SS, DS };
using Tag = uint32_t;
[[noreturn]] void ivt_write(uint32_t a, uint32_t last_entry);
struct Memory {
    static constexpr uint32_t kSize = 0x110000; // 1 MiB plus the HMA wrap area
    std::vector<uint8_t> b = std::vector<uint8_t>(kSize, 0);
    std::vector<Tag> tag = std::vector<Tag>(kSize, 0);
    bool watch_ivt = false;       // MTV_WATCH_IVT: stop on writes to the interrupt vector table
    uint32_t last_entry = 0;      // image offset of the last translated function entered
    static uint32_t lin(uint16_t seg, uint16_t off) { return ((uint32_t(seg) << 4) + off) & 0xFFFFF; }
    uint8_t r8(uint32_t a) const { return b[a]; }
    uint16_t r16(uint32_t a) const { return uint16_t(b[a] | b[(a + 1) & 0xFFFFF] << 8); }
    void w8(uint32_t a, uint8_t v, Tag t = 0) { if (watch_ivt && a < 0x400) ivt_write(a, last_entry); b[a] = v; tag[a] = t; }
    void w16(uint32_t a, uint16_t v, Tag t0 = 0, Tag t1 = 0) { if (watch_ivt && a < 0x400) ivt_write(a, last_entry); b[a] = uint8_t(v); tag[a] = t0; b[(a + 1) & 0xFFFFF] = uint8_t(v >> 8); tag[(a + 1) & 0xFFFFF] = t1; }
};
class Machine;
struct Cpu {
    std::array<uint16_t, 8> r{};      // AX CX DX BX SP BP SI DI
    std::array<uint16_t, 4> s{};      // ES CS SS DS
    std::array<Tag, 16> t{};          // tag per register byte: [reg*2 + 0] low, [reg*2 + 1] high
    bool cf = false, pf = false, af = false, zf = false, sf = false, of = false, df = false, ifl = true, tf = false;
    uint64_t icount = 0, next_event = ~0ull;
    Memory* m = nullptr;
    Machine* mach = nullptr;
    // Return addresses (linear) expected by the translated call sites. A RET/RETF/IRET that pops a different
    // address is a computed jump (e.g. `push es; push di; retf`) and is dispatched as one.
    std::vector<uint32_t> shadow;
    void enter(uint16_t ret_cs, uint16_t ret_ip) { shadow.push_back(Memory::lin(ret_cs, ret_ip)); }
    void leave() { shadow.pop_back(); }
    // byte registers: AL=0 CL=1 DL=2 BL=3 AH=4 CH=5 DH=6 BH=7
    uint8_t g8(uint8_t i) const { return i < 4 ? uint8_t(r[i]) : uint8_t(r[i - 4] >> 8); }
    void s8(uint8_t i, uint8_t v, Tag tg = 0) {
        if (i < 4) { r[i] = uint16_t((r[i] & 0xFF00) | v); t[i * 2] = tg; }
        else { r[i - 4] = uint16_t((r[i - 4] & 0x00FF) | (v << 8)); t[(i - 4) * 2 + 1] = tg; }
    }
    Tag t8(uint8_t i) const { return i < 4 ? t[i * 2] : t[(i - 4) * 2 + 1]; }
    void s16(uint8_t i, uint16_t v) { r[i] = v; t[i * 2] = t[i * 2 + 1] = 0; }
    uint32_t lin(uint8_t seg, uint16_t off) const { return Memory::lin(s[seg], off); }
    // tagged loads/stores used by mov, push/pop, xchg and string instructions
    void ld16(uint8_t reg, uint32_t a) { r[reg] = m->r16(a); t[reg * 2] = m->tag[a]; t[reg * 2 + 1] = m->tag[(a + 1) & 0xFFFFF]; }
    void st16(uint32_t a, uint8_t reg) { m->w16(a, r[reg], t[reg * 2], t[reg * 2 + 1]); }
    void ld8(uint8_t reg, uint32_t a) { s8(reg, m->r8(a), m->tag[a]); }
    void st8(uint32_t a, uint8_t reg) { m->w8(a, g8(reg), t8(reg)); }
    void push16(uint16_t v, Tag t0 = 0, Tag t1 = 0) { r[SP] = uint16_t(r[SP] - 2); m->w16(lin(SS, r[SP]), v, t0, t1); }
    uint16_t pop16() { uint16_t v = m->r16(lin(SS, r[SP])); r[SP] = uint16_t(r[SP] + 2); return v; }
    void pushr(uint8_t reg) { push16(r[reg], t[reg * 2], t[reg * 2 + 1]); }
    void popr(uint8_t reg) { uint32_t a = lin(SS, r[SP]); ld16(reg, a); r[SP] = uint16_t(r[SP] + 2); if (reg == SP) r[SP] = m->r16(a); }
    uint16_t flags() const {
        return uint16_t(0x0002 | (cf ? 1 : 0) | (pf ? 4 : 0) | (af ? 0x10 : 0) | (zf ? 0x40 : 0) | (sf ? 0x80 : 0) | (tf ? 0x100 : 0) |
                        (ifl ? 0x200 : 0) | (df ? 0x400 : 0) | (of ? 0x800 : 0) | 0xF000);
    }
    void set_flags(uint16_t f) {
        cf = f & 1; pf = f & 4; af = f & 0x10; zf = f & 0x40; sf = f & 0x80; tf = f & 0x100; ifl = f & 0x200; df = f & 0x400; of = f & 0x800;
    }
};
inline bool parity(uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return !(v & 1); }
// ---- arithmetic with flags (8 and 16 bit) ----
template <int B> struct W { using T = std::conditional_t<B == 8, uint8_t, uint16_t>; static constexpr uint32_t mask = B == 8 ? 0xFF : 0xFFFF, sign = B == 8 ? 0x80 : 0x8000; };
template <int B> inline void szp(Cpu& c, uint32_t r) { r &= W<B>::mask; c.zf = r == 0; c.sf = r & W<B>::sign; c.pf = parity(uint8_t(r)); }
template <int B> inline uint32_t add(Cpu& c, uint32_t a, uint32_t b, uint32_t carry = 0) {
    uint32_t r = a + b + carry;
    c.cf = r > W<B>::mask; c.af = ((a ^ b ^ r) & 0x10) != 0; c.of = ((a ^ r) & (b ^ r) & W<B>::sign) != 0; szp<B>(c, r);
    return r & W<B>::mask;
}
template <int B> inline uint32_t sub(Cpu& c, uint32_t a, uint32_t b, uint32_t borrow = 0) {
    uint32_t r = (a - b - borrow) & W<B>::mask;
    c.cf = uint64_t(b) + borrow > a; c.af = ((a ^ b ^ r) & 0x10) != 0; c.of = ((a ^ b) & (a ^ r) & W<B>::sign) != 0; szp<B>(c, r);
    return r;
}
template <int B> inline uint32_t logic(Cpu& c, uint32_t r) { c.cf = c.of = false; c.af = false; szp<B>(c, r); return r & W<B>::mask; }
template <int B> inline uint32_t inc(Cpu& c, uint32_t a) { bool cf = c.cf; uint32_t r = add<B>(c, a, 1); c.cf = cf; return r; }
template <int B> inline uint32_t dec(Cpu& c, uint32_t a) { bool cf = c.cf; uint32_t r = sub<B>(c, a, 1); c.cf = cf; return r; }
template <int B> inline uint32_t neg(Cpu& c, uint32_t a) { uint32_t r = sub<B>(c, 0, a); c.cf = a != 0; return r; }
template <int B> inline uint32_t shl(Cpu& c, uint32_t a, unsigned n) {
    n &= 0x1F; if (!n) return a;
    uint32_t r = a << n; c.cf = (n <= B) ? ((a >> (B - n)) & 1) : false; r &= W<B>::mask;
    c.of = ((r & W<B>::sign) != 0) != c.cf; c.af = false; szp<B>(c, r); return r;
}
template <int B> inline uint32_t shr(Cpu& c, uint32_t a, unsigned n) {
    n &= 0x1F; if (!n) return a;
    c.cf = (n <= B) ? ((a >> (n - 1)) & 1) : false; uint32_t r = n >= 32 ? 0 : (a >> n);
    c.of = (a & W<B>::sign) != 0; c.af = false; szp<B>(c, r); return r & W<B>::mask;
}
template <int B> inline uint32_t sar(Cpu& c, uint32_t a, unsigned n) {
    n &= 0x1F; if (!n) return a;
    int32_t sa = B == 8 ? int32_t(int8_t(a)) : int32_t(int16_t(a));
    c.cf = (sa >> std::min<unsigned>(n - 1, 31)) & 1; uint32_t r = uint32_t(sa >> std::min<unsigned>(n, 31)) & W<B>::mask;
    c.of = false; c.af = false; szp<B>(c, r); return r;
}
template <int B> inline uint32_t rol(Cpu& c, uint32_t a, unsigned n) {
    n &= 0x1F; if (!n) return a; unsigned k = n % B; uint32_t r = ((a << k) | (a >> (B - k))) & W<B>::mask;
    c.cf = r & 1; c.of = ((r & W<B>::sign) != 0) != c.cf; return r;
}
template <int B> inline uint32_t ror(Cpu& c, uint32_t a, unsigned n) {
    n &= 0x1F; if (!n) return a; unsigned k = n % B; uint32_t r = ((a >> k) | (a << (B - k))) & W<B>::mask;
    c.cf = (r & W<B>::sign) != 0; c.of = (((r << 1) ^ r) & W<B>::sign) != 0; return r;
}
template <int B> inline uint32_t rcl(Cpu& c, uint32_t a, unsigned n) {
    n = (n & 0x1F) % (B + 1); uint32_t r = a;
    for (unsigned i = 0; i < n; ++i) { bool nc = r & W<B>::sign; r = ((r << 1) | (c.cf ? 1 : 0)) & W<B>::mask; c.cf = nc; }
    if (n) c.of = ((r & W<B>::sign) != 0) != c.cf;
    return r;
}
template <int B> inline uint32_t rcr(Cpu& c, uint32_t a, unsigned n) {
    n = (n & 0x1F) % (B + 1); uint32_t r = a;
    if (n) c.of = ((r & W<B>::sign) != 0) != c.cf;
    for (unsigned i = 0; i < n; ++i) { bool nc = r & 1; r = (r >> 1) | (c.cf ? W<B>::sign : 0); c.cf = nc; }
    return r;
}
// mul/div (results in AX/DX like the hardware); divide errors raise INT 0
void mul8(Cpu& c, uint8_t v); void imul8(Cpu& c, uint8_t v); void mul16(Cpu& c, uint16_t v); void imul16(Cpu& c, uint16_t v);
bool div8(Cpu& c, uint8_t v); bool idiv8(Cpu& c, uint8_t v); bool div16(Cpu& c, uint16_t v); bool idiv16(Cpu& c, uint16_t v);
uint16_t imul16_3(Cpu& c, uint16_t a, uint16_t b);
void daa(Cpu& c); void das(Cpu& c); void aaa(Cpu& c); void aas(Cpu& c); void aam(Cpu& c, uint8_t base); void aad(Cpu& c, uint8_t base);
// ---- string instructions (one iteration; the generator emits the REP loops) ----
void movs8(Cpu& c, uint8_t seg); void movs16(Cpu& c, uint8_t seg);
void stos8(Cpu& c); void stos16(Cpu& c);
void lods8(Cpu& c, uint8_t seg); void lods16(Cpu& c, uint8_t seg);
void scas8(Cpu& c); void scas16(Cpu& c);
void cmps8(Cpu& c, uint8_t seg); void cmps16(Cpu& c, uint8_t seg);
[[noreturn]] void fatal(const Cpu& c, uint32_t at, const std::string& why);
extern bool g_trace_entries;
void trace_entry(const Cpu& c, uint32_t image_offset);
}
