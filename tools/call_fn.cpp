// mtv-tool call-fn <snapshot> <image offset hex>: run one translated function from a snapshot and write the
// final registers and memory next to it (<snapshot>.out). Interrupts abort the run.
#include "extract.h"
#include "rt/dos.h"
#include "mtv.h"
#include <cstdio>
#include <iostream>
#include <string>
#include "rt/logic.h"
namespace mtv::tool {
namespace {
struct Bare : rt::Machine {
    std::unordered_map<uint32_t, rt::Fn> fns;
    void interrupt(rt::Cpu& c, uint8_t n, uint16_t) override {
        if (n == 0x21 && (c.r[rt::AX] >> 8) == 0x4A) { c.cf = false; return; } // same minimal DOS as func_difftest.py
        rt::fatal(c, n, "interrupt in isolated function run");
    }
    std::vector<std::pair<uint16_t, uint8_t>> outs;
    uint8_t in8(rt::Cpu&, uint16_t) override { return 0; }  // same as func_difftest.py
    void out8(rt::Cpu&, uint16_t p, uint8_t v) override { outs.push_back({p, v}); }
    void events(rt::Cpu& c) override { c.next_event = ~0ull; }
    rt::Memory* mem = nullptr;
    rt::Fn find(uint16_t seg, uint16_t off) {
        auto it = fns.find(rt::Memory::lin(seg, off) - load_seg * 16u);
        if (it != fns.end()) return it->second;
        for (size_t m = 0; m < rt::static_logic().blob_count; ++m) {
            const auto& b = rt::static_logic().blobs[m];
            bool match = true;
            for (size_t i = 0; i < 64 && match; ++i) match = mem->r8(rt::Memory::lin(seg, uint16_t(i))) == b.signature[i];
            if (match) for (size_t i = 0; i < b.count; ++i) if (b.functions[i].image_offset == off) return b.functions[i].fn;
        }
        return nullptr;
    }
    void call_near(rt::Cpu& c, uint16_t ip) override { if (auto f = find(c.s[rt::CS], ip)) f(c); else rt::fatal(c, ip, "unknown near target"); }
    void call_far(rt::Cpu& c, uint16_t s, uint16_t o) override { if (auto f = find(s, o)) f(c); else rt::fatal(c, o, "unknown far target"); }
    void jump_far(rt::Cpu& c, uint16_t s, uint16_t o) override {
        if (s == 0xF000) { // chained to a BIOS IRET, as DosMachine::jump_far
            const uint16_t ip = c.pop16(), cs = c.pop16();
            c.s[rt::CS] = cs; c.set_flags(c.pop16());
            if (!c.shadow.empty() && rt::Memory::lin(cs, ip) == c.shadow.back()) return;
            if (auto f = find(cs, ip)) f(c);
            return;
        }
        if (auto f = find(s, o)) f(c);
    }
    void jump_near(rt::Cpu& c, uint16_t ip) override { if (auto f = find(c.s[rt::CS], ip)) f(c); }
};
}
int call_fn_command(std::span<const std::string_view> args) {
    if (args.size() != 2) { std::cerr << "usage: mtv-tool call-fn <snapshot> <image offset hex>\n"; return 2; }
    std::FILE* f = std::fopen(std::string(args[0]).c_str(), "rb");
    if (!f) return 1;
    uint16_t regs[13];
    rt::Memory mem;
    if (std::fread(regs, 2, 13, f) != 13 || std::fread(mem.b.data(), 1, 0x100000, f) != 0x100000) return 1;
    std::fclose(f);
    Bare m; m.load_seg = 0x824;
    for (size_t i = 0; i < rt::static_logic().function_count; ++i) m.fns[rt::static_logic().functions[i].image_offset] = rt::static_logic().functions[i].fn;
    rt::Cpu c; c.m = &mem; c.mach = &m;
    for (int i = 0; i < 8; ++i) c.r[size_t(i)] = regs[i];
    for (int i = 0; i < 4; ++i) c.s[size_t(i)] = regs[8 + i];
    c.set_flags(regs[12]);
    m.mem = &mem;
    std::string spec(args[1]);
    rt::Fn fn = nullptr;
    if (spec.rfind("blob:", 0) == 0) fn = m.find(c.s[rt::CS], uint16_t(std::stoul(spec.substr(5), nullptr, 16)));
    else if (auto it = m.fns.find(uint32_t(std::stoul(spec, nullptr, 16))); it != m.fns.end()) fn = it->second;
    if (!fn) { std::cerr << "no such function\n"; return 1; }
    try { fn(c); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 3; }
    for (int i = 0; i < 8; ++i) regs[i] = c.r[size_t(i)];
    for (int i = 0; i < 4; ++i) regs[8 + i] = c.s[size_t(i)];
    regs[12] = c.flags();
    std::FILE* o = std::fopen((std::string(args[0]) + ".out").c_str(), "wb");
    std::fwrite(regs, 2, 13, o); std::fwrite(mem.b.data(), 1, 0x100000, o); std::fclose(o);
    std::FILE* po = std::fopen((std::string(args[0]) + ".ports").c_str(), "w");
    for (auto [p, v] : m.outs) std::fprintf(po, "%x %x\n", p, v);
    std::fclose(po);
    std::cerr << "instructions " << c.icount << '\n';
    return 0;
}
}
