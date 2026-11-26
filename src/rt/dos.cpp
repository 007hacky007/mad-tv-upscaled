#include "rt/dos.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
namespace mtv::rt {
namespace {
const bool kTrace = std::getenv("MTV_TRACE") != nullptr;
constexpr uint16_t kBios = 0xF000;     // native BIOS/DOS stubs live at F000:(vector * 4)
constexpr uint16_t kEnv = 0x0800;
constexpr uint16_t kMemTop = kHostSegment; // DOS arena ends here; above it is the host area
std::string upper(std::string s) { for (auto& ch : s) ch = char(std::toupper(uint8_t(ch))); return s; }
bool wildcard(const std::string& pat, const std::string& name) {
    // DOS 8.3 matching for patterns like "*.RSC" or "SAVE.?".
    auto split = [](const std::string& s) { auto d = s.find('.'); return std::make_pair(s.substr(0, d), d == std::string::npos ? std::string() : s.substr(d + 1)); };
    auto part = [](const std::string& p, const std::string& n) {
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '*') return true;
            if (i >= n.size()) return p[i] == '?' && std::all_of(p.begin() + long(i), p.end(), [](char c) { return c == '?'; });
            if (p[i] != '?' && p[i] != n[i]) return false;
        }
        return p.size() == n.size();
    };
    auto [pb, pe] = split(upper(pat)); auto [nb, ne] = split(upper(name));
    return part(pb, nb) && part(pe, ne);
}
}
DosMachine::DosMachine(Config cfg, const LogicTables& logic, uint32_t entry, std::vector<uint8_t> exe)
    : cfg_(std::move(cfg)), logic_(logic), entry_(entry), exe_(std::move(exe)) {
    for (size_t i = 0; i < logic.function_count; ++i) static_fns_[logic.functions[i].image_offset] = logic.functions[i].fn;
    cpu.m = &mem; cpu.mach = this;
    std::filesystem::create_directories(cfg_.user_dir);
    load_program(exe_);
}
DosMachine::~DosMachine() {
    if (thread_.joinable()) {
        { std::unique_lock l(mu_); stop_ = true; host_turn_ = false; }
        cv_.notify_all();
        thread_.join();
    }
}
void DosMachine::load_program(const std::vector<uint8_t>& exe) {
    // Interrupt vectors point at native stubs; BIOS data area; DOS memory with one block owned by the program.
    for (uint16_t v = 0; v < 256; ++v) { mem.w16(v * 4u, uint16_t(v * 4)); mem.w16(v * 4u + 2, kBios); }
    mem.w16(0x410, 0x0021); mem.w8(0x449, 3);
    const uint32_t ticks = uint32_t(12 * 3600 * 18.2065);
    mem.w16(0x46C, uint16_t(ticks)); mem.w16(0x46E, uint16_t(ticks >> 16));
    const uint16_t hdr = uint16_t(exe[8] | exe[9] << 8) * 16;
    load_seg = uint16_t(kEnv + 0x24);       // same layout as the DOSBox reference (PSP 0814, image 0824)
    psp_ = uint16_t(load_seg - 0x10);
    // environment with the program path (Turbo C derives argv[0] from it)
    const char env[] = "COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0\x01\0C:\\MTV.EXE\0";
    for (size_t i = 0; i < sizeof env; ++i) mem.w8(kEnv * 16u + uint32_t(i), uint8_t(env[i]));
    // PSP
    const uint32_t p = psp_ * 16u;
    mem.w8(p, 0xCD); mem.w8(p + 1, 0x20); mem.w16(p + 2, kMemTop); mem.w16(p + 0x2C, kEnv);
    const std::string& tail = cfg_.command_tail;
    mem.w8(p + 0x80, uint8_t(tail.size()));
    for (size_t i = 0; i < tail.size(); ++i) mem.w8(p + 0x81 + uint32_t(i), uint8_t(tail[i]));
    mem.w8(p + 0x81 + uint32_t(tail.size()), 0x0D);
    dta_seg_ = psp_;
    // load module and fixups
    const size_t image = exe.size() - hdr;
    for (size_t i = 0; i < image; ++i) mem.w8(load_seg * 16u + uint32_t(i), exe[hdr + i]);
    const uint16_t nrel = uint16_t(exe[6] | exe[7] << 8), roff = uint16_t(exe[0x18] | exe[0x19] << 8);
    for (uint16_t i = 0; i < nrel; ++i) {
        const uint16_t o = uint16_t(exe[roff + 4 * i] | exe[roff + 4 * i + 1] << 8), s = uint16_t(exe[roff + 4 * i + 2] | exe[roff + 4 * i + 3] << 8);
        const uint32_t a = (load_seg + s) * 16u + o;
        mem.w16(a, uint16_t(mem.r16(a) + load_seg));
    }
    blocks_ = {{psp_, uint16_t(kMemTop - psp_), true}};
    mem.watch_ivt = std::getenv("MTV_WATCH_IVT") != nullptr;
    auto w = [&](size_t at) { return uint16_t(exe[at] | exe[at + 1] << 8); };
    cpu.s[SS] = uint16_t(w(0x0E) + load_seg); cpu.r[SP] = w(0x10);
    cpu.s[CS] = uint16_t(w(0x16) + load_seg);
    cpu.s[DS] = cpu.s[ES] = psp_;
    cpu.ifl = true;
    next_frame_ = frame_period();
    next_irq_ = uint64_t(0x10000) * cfg_.cycles_per_ms * 1000 / 1193182; // BIOS default 18.2 Hz until reprogrammed
    reschedule(cpu);
}
std::filesystem::path DosMachine::resolve(const std::string& name, bool for_write) {
    std::string n = name;
    if (n.size() > 1 && n[1] == ':') n = n.substr(2);
    while (!n.empty() && (n[0] == '\\' || n[0] == '/')) n = n.substr(1);
    n = upper(n);
    auto find_in = [&](const std::filesystem::path& dir) -> std::optional<std::filesystem::path> {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (upper(e.path().filename().string()) == n) return e.path();
        return std::nullopt;
    };
    auto user = find_in(cfg_.user_dir);
    if (user) return *user;
    auto game = find_in(cfg_.game_dir);
    if (for_write) {
        auto dst = cfg_.user_dir / n;
        if (game) std::filesystem::copy_file(*game, dst, std::filesystem::copy_options::overwrite_existing);
        return dst;
    }
    return game ? *game : std::filesystem::path();
}
std::string DosMachine::read_string(uint16_t seg, uint16_t off) {
    std::string s;
    for (uint16_t i = 0; i < 256; ++i) { char ch = char(mem.r8(Memory::lin(seg, uint16_t(off + i)))); if (!ch) break; s += ch; }
    return s;
}
void DosMachine::set_carry(Cpu& c, bool error, uint16_t ax) { c.cf = error; if (error) c.s16(AX, ax); }
Fn DosMachine::lookup(uint16_t seg, uint16_t off) {
    const int64_t img = int64_t(Memory::lin(seg, off)) - int64_t(load_seg) * 16;
    if (img >= 0) if (auto it = static_fns_.find(uint32_t(img)); it != static_fns_.end()) return it->second;
    // run-time loaded code: identify the module by its first 64 bytes at seg:0
    for (size_t m = 0; m < logic_.blob_count; ++m) {
        const auto& b = logic_.blobs[m];
        bool match = true;
        for (size_t i = 0; i < 64 && match; ++i) match = mem.r8(Memory::lin(seg, uint16_t(i))) == b.signature[i];
        if (!match) continue;
        for (size_t i = 0; i < b.count; ++i) if (b.functions[i].image_offset == off) return b.functions[i].fn;
    }
    return nullptr;
}
void DosMachine::call_near(Cpu& c, uint16_t ip) {
    if (kTrace) std::fprintf(stderr, "near %04X:%04X at %llu\n", c.s[CS], ip, (unsigned long long)c.icount);
    if (Fn f = lookup(c.s[CS], ip)) { f(c); return; }
    fatal(c, ip, "near call to untranslated code");
}
// A jump to an address that is not a translated function entry is a hand-made return (Turbo C's _setargv and
// _setenvp pop their return address and jump back to it): the C++ caller resumes exactly there.
void DosMachine::jump_near(Cpu& c, uint16_t ip) {
    if (Fn f = lookup(c.s[CS], ip)) { f(c); return; }
    if (!c.shadow.empty() && Memory::lin(c.s[CS], ip) == c.shadow.back()) return; // hand-made return
    fatal(c, ip, "jump to an address that is neither a function nor the return address");
}
void DosMachine::call_far(Cpu& c, uint16_t seg, uint16_t off) {
    if (kTrace) std::fprintf(stderr, "far  %04X:%04X at %llu\n", seg, off, (unsigned long long)c.icount);
    if (Fn f = lookup(seg, off)) { f(c); return; }
    fatal(c, off, "far call to untranslated code at segment " + std::to_string(seg));
}
void DosMachine::jump_far(Cpu& c, uint16_t seg, uint16_t off) {
    if (seg == kBios) {
        // A handler chaining to the BIOS (the Palladix timer handler chains INT 8): do the BIOS work, then IRET.
        // The IRET returns either to the interrupted program (the shadow return: the C++ caller resumes there) or
        // to a frame the handler built itself (pushf; push cs; push ip; jmp far), where execution continues.
        if (off / 4 == 8) bios_tick(c);
        const uint16_t ip = c.pop16(), cs = c.pop16();
        c.s[CS] = cs; c.set_flags(c.pop16());
        if (!c.shadow.empty() && Memory::lin(cs, ip) == c.shadow.back()) return;
        if (Fn f = lookup(cs, ip)) { f(c); return; }
        fatal(c, ip, "BIOS IRET to untranslated code at segment " + std::to_string(cs));
    }
    if (Fn f = lookup(seg, off)) { f(c); return; }
    if (!c.shadow.empty() && Memory::lin(seg, off) == c.shadow.back()) return; // hand-made return
    fatal(c, off, "far jump to an address that is neither a function nor the return address");
}
void DosMachine::bios_tick(Cpu&) {
    uint32_t t = uint32_t(mem.r16(0x46C)) | uint32_t(mem.r16(0x46E)) << 16;
    ++t;
    mem.w16(0x46C, uint16_t(t)); mem.w16(0x46E, uint16_t(t >> 16));
}
void DosMachine::deliver(Cpu& c, uint8_t n) {
    const uint16_t off = mem.r16(n * 4u), seg = mem.r16(n * 4u + 2);
    static unsigned long count = 0;
    if (kTrace && (++count % 1000) == 1) std::fprintf(stderr, "IRQ %u -> %04X:%04X (#%lu, divisor %u, IF %d)\n", n, seg, off, count, pit_divisor_, c.ifl);
    if (seg == kBios) { if (n == 8) bios_tick(c); return; }
    static const bool irqlog = std::getenv("MTV_IRQLOG") != nullptr;
    if (irqlog) std::fprintf(stderr, "irq %llu due %llu\n", (unsigned long long)c.icount, (unsigned long long)next_irq_);
    if (record_irqs_ && n == 8) irq_times_.push_back(c.icount);
    const uint16_t saved_cs = c.s[CS];
    c.push16(c.flags()); c.push16(saved_cs); c.push16(0);
    c.ifl = false; c.tf = false;
    c.s[CS] = seg;
    c.enter(saved_cs, 0);
    ++irq_depth_;
    call_far(c, seg, off);
    --irq_depth_;
    c.leave();
}
void DosMachine::reschedule(Cpu& c) {
    c.next_event = next_frame_;
    if (next_irq_) c.next_event = std::min(c.next_event, next_irq_);
}
void DosMachine::events(Cpu& c) {
    for (;;) {
        if (next_irq_ && c.icount >= next_irq_ && !c.ifl && std::getenv("MTV_IFLOG")) {
            static std::map<std::pair<uint16_t, uint32_t>, int> seen;
            if (++seen[{c.s[CS], mem.last_entry}] == 1) std::fprintf(stderr, "IF=0 with IRQ due: CS=%04X in %05X at %llu\n", c.s[CS], mem.last_entry, (unsigned long long)c.icount);
        }
        if (next_irq_ && c.icount >= next_irq_ && !c.ifl && kTrace) { static int w = 0; if (++w % 100000 == 1) std::fprintf(stderr, "IRQ pending with IF=0 at %llu\n", (unsigned long long)c.icount); }
        if (next_irq_ && c.icount >= next_irq_ && c.ifl) {
            const uint32_t div = pit_divisor_ ? pit_divisor_ : 0x10000;
            next_irq_ += std::max<uint64_t>(1, uint64_t(div) * cfg_.cycles_per_ms * 1000 / 1193182);
            deliver(c, 8);
            continue;
        }
        if (c.icount >= next_frame_) {
            next_frame_ += frame_period();
            ++frames_;
            yield(c);
            continue;
        }
        break;
    }
    reschedule(c);
}
void DosMachine::yield(Cpu&) {
    frame_.mode = video_mode_;
    frame_.index = frames_;
    frame_.dac = dac_;
    if (video_mode_ == 0x13) {
        frame_.pixels.assign(mem.b.begin() + 0xA0000, mem.b.begin() + 0xA0000 + 64000);
        frame_.tags.assign(mem.tag.begin() + 0xA0000, mem.tag.begin() + 0xA0000 + 64000);
    } else {
        frame_.pixels.clear(); frame_.tags.clear();
    }
    std::unique_lock l(mu_);
    host_turn_ = true;
    cv_.notify_all();
    cv_.wait(l, [&] { return !host_turn_; });
    if (stop_) throw ProgramExit{-2};
}
bool DosMachine::run_frame(const HostInput& in) {
    if (exit_code_) return false;
    {
        std::unique_lock l(mu_);
        input_ = in;
        for (auto k : in.keys) keys_.push_back(k);
        mouse_x_ = std::clamp(in.mouse_x * (mouse_max_x_ + 1) / 320, mouse_min_x_, mouse_max_x_);
        mouse_y_ = std::clamp(in.mouse_y * (mouse_max_y_ + 1) / 200, mouse_min_y_, mouse_max_y_);
        mouse_buttons_ = in.buttons;
        if (!started_) {
            started_ = true;
            thread_ = std::thread([this] {
                { std::unique_lock l2(mu_); cv_.wait(l2, [&] { return !host_turn_; }); }
                try {
                    Fn entry = static_fns_.at(entry_);
                    entry(cpu);
                    exit_code_ = 0;
                } catch (const ProgramExit& e) {
                    exit_code_ = e.code;
                } catch (const std::exception& e) {
                    error_ = e.what();
                    exit_code_ = -1;
                    if (log) log(std::string("program stopped: ") + e.what());
                }
                std::unique_lock l3(mu_);
                host_turn_ = true;
                cv_.notify_all();
            });
        }
        host_turn_ = false;
    }
    cv_.notify_all();
    std::unique_lock l(mu_);
    cv_.wait(l, [&] { return host_turn_; });
    return !exit_code_;
}
bool replace_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        o.flush();
        if (!o) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    }
    std::error_code ec;
    if (std::filesystem::file_size(tmp, ec) != bytes.size() || ec) { std::filesystem::remove(tmp, ec); return false; }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}
// ---------------- interrupts ----------------
void DosMachine::interrupt(Cpu& c, uint8_t n, uint16_t ret_ip) {
    const uint16_t off = mem.r16(n * 4u), seg = mem.r16(n * 4u + 2);
    if (seg != kBios) {  // a handler installed by the program
        const uint16_t saved_cs = c.s[CS];
        c.push16(c.flags()); c.push16(saved_cs); c.push16(ret_ip);
        c.ifl = false; c.tf = false;
        c.s[CS] = seg;
        c.enter(saved_cs, ret_ip);
        call_far(c, seg, off);
        c.leave();
        return;
    }
    switch (n) {
    case 0x21: dos(c); break;
    case 0x10: video(c); break;
    case 0x16: keyboard(c); break;
    case 0x33: mouse(c); break;
    case 0x11: c.s16(AX, mem.r16(0x410)); break;
    case 0x1A:
        if ((c.r[AX] >> 8) == 0) { c.s16(CX, mem.r16(0x46E)); c.s16(DX, mem.r16(0x46C)); c.s8(AL, 0); }
        break;
    case 0x00: throw ProgramExit{-3}; // divide error without a handler
    default:
        if (log) log("unhandled INT " + std::to_string(n) + " AX=" + std::to_string(c.r[AX]));
    }
}
void DosMachine::dos(Cpu& c) {
    const uint8_t ah = uint8_t(c.r[AX] >> 8), al = uint8_t(c.r[AX]);
    if (kTrace) std::fprintf(stderr, "DOS AH=%02X AL=%02X BX=%04X CX=%04X DX=%04X DS=%04X ES=%04X at %llu\n", ah, al, c.r[BX], c.r[CX], c.r[DX], c.s[DS], c.s[ES], (unsigned long long)c.icount);
    switch (ah) {
    case 0x02: break;
    case 0x06: if (uint8_t(c.r[DX]) == 0xFF) { c.zf = true; c.s8(AL, 0); } break;
    case 0x09: if (log) log("DOS print: " + read_string(c.s[DS], c.r[DX])); break;
    case 0x0B: c.s8(AL, 0); break;
    case 0x0E: c.s8(AL, 26); break;
    case 0x19: c.s8(AL, 2); break;
    case 0x1A: dta_seg_ = c.s[DS]; dta_off_ = c.r[DX]; break;
    case 0x25: { bool w = mem.watch_ivt; mem.watch_ivt = false; mem.w16(al * 4u, c.r[DX]); mem.w16(al * 4u + 2, c.s[DS]); mem.watch_ivt = w; break; }
    case 0x2A: c.s16(CX, 1994); c.s16(DX, 0x020A); c.s8(AL, 4); break;
    case 0x2C: {
        const uint64_t ms = c.icount / cfg_.cycles_per_ms;
        const uint64_t s = 12 * 3600 + ms / 1000;
        c.s16(CX, uint16_t(((s / 3600) % 24) << 8 | ((s / 60) % 60))); c.s16(DX, uint16_t((s % 60) << 8 | ((ms % 1000) / 10)));
        break;
    }
    case 0x2F: c.s[ES] = dta_seg_; c.s16(BX, dta_off_); break;
    case 0x30: c.s16(AX, 0x0005); c.s16(BX, 0); c.s16(CX, 0); break;
    case 0x33: c.s8(DL, 0); break;
    case 0x35: c.s16(BX, mem.r16(al * 4u)); c.s[ES] = mem.r16(al * 4u + 2); break;
    case 0x3C: case 0x3D: {
        const std::string name = read_string(c.s[DS], c.r[DX]);
        const bool write = ah == 0x3C || (al & 3) != 0;
        auto path = resolve(name, write);
        OpenFile f{path, {}, 0, false};
        if (ah == 0x3C) { f.dirty = true; }
        else {
            std::ifstream in(path, std::ios::binary);
            if (!in) { set_carry(c, true, 2); break; }
            f.data.assign(std::istreambuf_iterator<char>(in), {});
        }
        uint16_t h = 5; while (files_.count(h)) ++h;
        files_[h] = std::move(f);
        c.s16(AX, h); c.cf = false;
        break;
    }
    case 0x3E: {
        auto it = files_.find(c.r[BX]);
        if (it == files_.end()) { set_carry(c, c.r[BX] > 4, 6); break; }
        // the game's saves live in this file: replace it whole, and tell the game when that failed
        const bool ok = !it->second.dirty || replace_file(it->second.path, it->second.data);
        files_.erase(it);
        if (ok) c.cf = false; else set_carry(c, true, 0x1D); // write fault
        break;
    }
    case 0x3F: {
        auto it = files_.find(c.r[BX]);
        if (it == files_.end()) { set_carry(c, true, 6); break; }
        auto& f = it->second;
        const uint32_t n = std::min<uint32_t>(c.r[CX], uint32_t(f.data.size() > f.pos ? f.data.size() - f.pos : 0));
        if (n) {
            std::string name = f.path.filename().string();
            for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            reads_.push_back({std::move(name), f.pos, n});
        }
        for (uint32_t i = 0; i < n; ++i) mem.w8(Memory::lin(c.s[DS], uint16_t(c.r[DX] + i)), f.data[f.pos + i]);
        f.pos += n; c.s16(AX, uint16_t(n)); c.cf = false;
        break;
    }
    case 0x40: {
        if (c.r[BX] <= 4) { c.s16(AX, c.r[CX]); c.cf = false; break; }
        auto it = files_.find(c.r[BX]);
        if (it == files_.end()) { set_carry(c, true, 6); break; }
        auto& f = it->second;
        if (f.pos + c.r[CX] > f.data.size()) f.data.resize(f.pos + c.r[CX]);
        for (uint32_t i = 0; i < c.r[CX]; ++i) f.data[f.pos + i] = mem.r8(Memory::lin(c.s[DS], uint16_t(c.r[DX] + i)));
        if (c.r[CX] == 0) f.data.resize(f.pos);
        f.pos += c.r[CX]; f.dirty = true; c.s16(AX, c.r[CX]); c.cf = false;
        break;
    }
    case 0x41: { std::error_code ec; std::filesystem::remove(resolve(read_string(c.s[DS], c.r[DX]), true), ec); c.cf = false; break; }
    case 0x42: {
        auto it = files_.find(c.r[BX]);
        if (it == files_.end()) { set_carry(c, true, 6); break; }
        auto& f = it->second;
        const int64_t off = int32_t(uint32_t(c.r[CX]) << 16 | c.r[DX]);
        int64_t base = al == 0 ? 0 : al == 1 ? int64_t(f.pos) : int64_t(f.data.size());
        f.pos = uint32_t(std::max<int64_t>(0, base + off));
        c.s16(AX, uint16_t(f.pos)); c.s16(DX, uint16_t(f.pos >> 16)); c.cf = false;
        break;
    }
    case 0x43: c.s16(CX, 0x20); c.cf = false; break;
    case 0x44:
        if (al == 0) { c.s16(DX, c.r[BX] <= 4 ? 0x80D3 : 0x0002); c.cf = false; }
        else c.cf = false;
        break;
    case 0x47: mem.w8(Memory::lin(c.s[DS], c.r[SI]), 0); c.cf = false; break;
    case 0x48: case 0x4A: {
        // first-fit over a simple block list below A000
        auto largest_free = [&] {
            uint16_t best = 0; uint16_t end = kMemTop;
            std::vector<Block> v = blocks_; std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.seg < b.seg; });
            uint16_t cur = psp_;
            for (auto& b : v) { if (b.seg > cur) best = std::max<uint16_t>(best, uint16_t(b.seg - cur)); cur = uint16_t(b.seg + b.paras); }
            if (end > cur) best = std::max<uint16_t>(best, uint16_t(end - cur));
            return best;
        };
        if (ah == 0x4A) {
            auto it = std::find_if(blocks_.begin(), blocks_.end(), [&](auto& b) { return b.seg == c.s[ES]; });
            if (it == blocks_.end()) { set_carry(c, true, 9); break; }
            uint16_t limit = kMemTop;
            for (auto& b : blocks_) if (b.seg > it->seg) limit = std::min(limit, b.seg);
            if (it->seg + uint32_t(c.r[BX]) > limit) { set_carry(c, true, 8); c.s16(BX, uint16_t(limit - it->seg)); if (kTrace) std::fprintf(stderr, "   resize FAILED\n"); break; }
            it->paras = c.r[BX]; c.cf = false;
            if (it->seg != psp_ || frames_ > 0) peak_seg_ = std::max<uint32_t>(peak_seg_, uint32_t(it->seg) + it->paras);
            if (kTrace) std::fprintf(stderr, "   resize ok %04X paras at %04X\n", c.r[BX], it->seg);
            break;
        }
        std::vector<Block> v = blocks_; std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.seg < b.seg; });
        uint16_t cur = v.front().seg + v.front().paras; bool ok = false;
        for (size_t i = 1; i <= v.size(); ++i) {
            const uint16_t next = i < v.size() ? v[i].seg : kMemTop;
            if (next >= cur && next - cur >= c.r[BX]) { blocks_.push_back({cur, c.r[BX], true}); c.s16(AX, cur); c.cf = false; ok = true; peak_seg_ = std::max<uint32_t>(peak_seg_, uint32_t(cur) + c.r[BX]); break; }
            if (i < v.size()) cur = uint16_t(v[i].seg + v[i].paras);
        }
        if (!ok) { set_carry(c, true, 8); c.s16(BX, largest_free()); }
        break;
    }
    case 0x49: {
        auto it = std::find_if(blocks_.begin(), blocks_.end(), [&](auto& b) { return b.seg == c.s[ES]; });
        if (it != blocks_.end() && it->seg != psp_) blocks_.erase(it);
        c.cf = false;
        break;
    }
    case 0x4C: throw ProgramExit{al};
    case 0x4E: case 0x4F: {
        if (ah == 0x4E) {
            const std::string pat = read_string(c.s[DS], c.r[DX]);
            std::map<std::string, std::filesystem::path> found;
            for (auto dir : {cfg_.game_dir, cfg_.user_dir}) {
                std::error_code ec;
                for (const auto& e : std::filesystem::directory_iterator(dir, ec))
                    if (e.is_regular_file() && wildcard(pat.substr(pat.find_last_of("\\:") == std::string::npos ? 0 : pat.find_last_of("\\:") + 1), e.path().filename().string()))
                        found[upper(e.path().filename().string())] = e.path();
            }
            find_results_.clear();
            for (auto& [k, v] : found) find_results_.push_back(resolve(k, false));
            find_pos_ = 0;
        }
        if (find_pos_ >= find_results_.size()) { set_carry(c, true, 18); break; }
        const auto& path = find_results_[find_pos_++];
        const uint32_t d = Memory::lin(dta_seg_, dta_off_);
        const uint32_t size = uint32_t(std::filesystem::file_size(path));
        mem.w8(d + 0x15, 0x20); mem.w16(d + 0x16, 0); mem.w16(d + 0x18, 0x1C4A);
        mem.w16(d + 0x1A, uint16_t(size)); mem.w16(d + 0x1C, uint16_t(size >> 16));
        const std::string nm = upper(path.filename().string());
        for (size_t i = 0; i < 13; ++i) mem.w8(d + 0x1E + uint32_t(i), i < nm.size() ? uint8_t(nm[i]) : 0);
        c.cf = false;
        break;
    }
    case 0x56: {
        std::error_code ec;
        std::filesystem::rename(resolve(read_string(c.s[DS], c.r[DX]), true), cfg_.user_dir / upper(read_string(c.s[ES], c.r[DI])), ec);
        c.cf = bool(ec);
        break;
    }
    case 0x57: if (al == 0) { c.s16(CX, 0); c.s16(DX, 0x1C4A); } c.cf = false; break;
    default:
        throw std::runtime_error("unsupported DOS function AH=" + std::to_string(ah));
    }
}
void DosMachine::video(Cpu& c) {
    const uint8_t ah = uint8_t(c.r[AX] >> 8), al = uint8_t(c.r[AX]);
    switch (ah) {
    case 0x00:
        video_mode_ = al & 0x7F; mem.w8(0x449, video_mode_);
        if (video_mode_ == 0x13 && !(al & 0x80)) for (uint32_t i = 0; i < 64000; ++i) mem.w8(0xA0000 + i, 0);
        break;
    case 0x0F: c.s16(AX, uint16_t((video_mode_ == 0x13 ? 40 : 80) << 8 | video_mode_)); c.s8(BH, 0); break;
    case 0x10:
        if (al == 0x10) dac_[uint8_t(c.r[BX])] = {uint8_t(c.r[DX] >> 8 & 63), uint8_t(c.r[CX] >> 8 & 63), uint8_t(c.r[CX] & 63)};
        else if (al == 0x12) {
            for (uint16_t i = 0; i < c.r[CX]; ++i)
                for (int k = 0; k < 3; ++k) dac_[uint8_t(c.r[BX] + i)][size_t(k)] = mem.r8(Memory::lin(c.s[ES], uint16_t(c.r[DX] + i * 3 + k))) & 63;
        } else if (al == 0x15) { auto& e = dac_[uint8_t(c.r[BX])]; c.s8(DH, e[0]); c.s8(CH, e[1]); c.s8(CL, e[2]); }
        else if (al == 0x17) {
            for (uint16_t i = 0; i < c.r[CX]; ++i)
                for (int k = 0; k < 3; ++k) mem.w8(Memory::lin(c.s[ES], uint16_t(c.r[DX] + i * 3 + k)), dac_[uint8_t(c.r[BX] + i)][size_t(k)]);
        }
        break;
    case 0x1A: c.s8(AL, 0x1A); c.s16(BX, 0x0008); break;
    default: if (log) log("unhandled INT 10h AH=" + std::to_string(ah));
    }
}
void DosMachine::keyboard(Cpu& c) {
    const uint8_t ah = uint8_t(c.r[AX] >> 8);
    switch (ah) {
    case 0x00: case 0x10:
        while (keys_.empty()) yield(c);
        c.s16(AX, keys_.front()); keys_.pop_front();
        break;
    case 0x01: case 0x11:
        if (keys_.empty()) c.zf = true; else { c.zf = false; c.s16(AX, keys_.front()); }
        break;
    case 0x02: c.s8(AL, 0); break;
    default: break;
    }
}
void DosMachine::mouse(Cpu& c) {
    if (kTrace && c.r[AX] != 3) std::fprintf(stderr, "MOUSE AX=%04X CX=%04X DX=%04X at %llu\n", c.r[AX], c.r[CX], c.r[DX], (unsigned long long)c.icount);
    switch (c.r[AX]) {
    case 0x00: c.s16(AX, 0xFFFF); c.s16(BX, 2); break;
    case 0x01: case 0x02: break;
    case 0x03: {
        // Mad TV reads the mouse relative to the centre (1600, 1600) and recentres every frame (0x473B9:
        // delta = pos / 8 - 200, accumulated at DS:7A48/7A4A and drained into the cursor at DS:7BF6/7BF8).
        // An absolute host pointer is turned into the delta that brings the game's cursor to it.
        const uint16_t ds = c.s[DS];
        auto steer = [&](int target, uint16_t cursor, uint16_t pending, int lo, int hi) {
            const int cur = int16_t(mem.r16(Memory::lin(ds, cursor))) + int16_t(mem.r16(Memory::lin(ds, pending)));
            return std::clamp(1600 + std::clamp(target - cur, -200, 199) * 8, lo, hi);
        };
        mouse_x_ = steer(input_.mouse_x, 0x7BF6, 0x7A48, mouse_min_x_, mouse_max_x_);
        mouse_y_ = steer(input_.mouse_y, 0x7BF8, 0x7A4A, mouse_min_y_, mouse_max_y_);
        c.s16(BX, mouse_buttons_); c.s16(CX, uint16_t(mouse_x_)); c.s16(DX, uint16_t(mouse_y_));
        break;
    }
    case 0x04: mouse_x_ = std::clamp<int>(c.r[CX], mouse_min_x_, mouse_max_x_); mouse_y_ = std::clamp<int>(c.r[DX], mouse_min_y_, mouse_max_y_); break;
    case 0x07: mouse_min_x_ = std::min<int>(c.r[CX], c.r[DX]); mouse_max_x_ = std::max<int>(c.r[CX], c.r[DX]); break;
    case 0x08: mouse_min_y_ = std::min<int>(c.r[CX], c.r[DX]); mouse_max_y_ = std::max<int>(c.r[CX], c.r[DX]); break;
    case 0x0F: break;
    default: if (log) log("unhandled INT 33h AX=" + std::to_string(c.r[AX]));
    }
}
// ---------------- ports ----------------
uint16_t DosMachine::ch2_count(const Cpu& c) const {
    // Counts down from the reload value at 1.193182 MHz while the gate is open (mode 2, 0 = 65536).
    const uint64_t cycles = ch2_frozen_ + (ch2_running_ ? c.icount - ch2_start_ : 0);
    const uint64_t ticks = cycles * 1193182 / (uint64_t(cfg_.cycles_per_ms) * 1000);
    const uint32_t reload = ch2_reload_ ? ch2_reload_ : 0x10000;
    return uint16_t(reload - ticks % reload);
}
uint8_t DosMachine::in8(Cpu& c, uint16_t port) {
    if (kTrace && port == 0x388) {
        static int n = 0; static uint8_t last = 0xFF;
        uint8_t v = opl_status_;
        if (++n < 400 && v != last) std::fprintf(stderr, "IN 388 (status before %02X) at %llu\n", v, (unsigned long long)c.icount);
        last = v;
    }
    switch (port) {
    case 0x3DA: {
        // Vertical retrace is the first 1/50 of each 70 Hz frame. Outside it, skip ahead to the next retrace:
        // the original spins in a polling loop until then, so the program state is the same.
        // A timer interrupt due before then fires at its own time: stop there and report no retrace yet, so the
        // polling loop goes on (music timing depends on it).
        const uint64_t P = frame_period(), phase = c.icount % P;
        if (phase < P / 50) return 0x09;
        const uint64_t retrace = (c.icount / P + 1) * P;
        if (next_irq_ && c.ifl && next_irq_ < retrace) {
            c.icount = std::max(c.icount, next_irq_);
            events(c);
            return 0x01;
        }
        c.icount = retrace;
        if (c.icount >= c.next_event) events(c);
        return 0x01;
    }
    case 0x3C9: { uint8_t v = dac_[dac_read_][dac_comp_]; if (++dac_comp_ == 3) { dac_comp_ = 0; ++dac_read_; } return v; }
    case 0x388: {
        c.icount += cfg_.cycles_per_ms / 1000; // an ISA bus access takes about 1 us; drivers time their delays with it
        const uint64_t us = c.icount * 1000 / cfg_.cycles_per_ms;
        uint8_t st = opl_status_;
        if (opl_t1_on_ && !(opl_regs_[4] & 0x40) && us - opl_t1_start_ >= uint64_t(256 - opl_regs_[2]) * 80) st |= 0xC0;
        if (opl_t2_on_ && !(opl_regs_[4] & 0x20) && us - opl_t2_start_ >= uint64_t(256 - opl_regs_[3]) * 320) st |= 0xA0;
        opl_status_ = st;
        return st;
    }
    case 0x60: return 0;
    case 0x61: return port61_;
    case 0x42: {
        const uint16_t n = ch2_count(c);
        const uint8_t v = ch2_read_low_ ? uint8_t(n) : uint8_t(n >> 8);
        ch2_read_low_ = !ch2_read_low_;
        return v;
    }
    default: return 0xFF;
    }
}
void DosMachine::out8(Cpu& c, uint16_t port, uint8_t v) {
    if (kTrace && port == 0x389) { static int n = 0; if (++n < 12) std::fprintf(stderr, "OPL reg %02X = %02X at %llu\n", opl_index_, v, (unsigned long long)c.icount); }
    if (kTrace && (port == 0x40 || port == 0x43)) { static int n = 0; if (++n < 400) std::fprintf(stderr, "OUT %03X,%02X at %llu\n", port, v, (unsigned long long)c.icount); }
    switch (port) {
    case 0x3C8: dac_write_ = v; dac_comp_ = 0; break;
    case 0x3C7: dac_read_ = v; dac_comp_ = 0; break;
    case 0x3C9: dac_[dac_write_][dac_comp_] = v & 63; if (++dac_comp_ == 3) { dac_comp_ = 0; ++dac_write_; } break;
    case 0x43:
        if ((v & 0xC0) == 0) pit_low_ = true;
        if ((v >> 6) == 2) { ch2_write_low_ = true; ch2_read_low_ = true; }
        break;
    case 0x40:
        if (pit_low_) { pit_latch_ = v; pit_low_ = false; }
        else {
            pit_divisor_ = uint16_t(pit_latch_ | v << 8); pit_low_ = true;
            const uint32_t div = pit_divisor_ ? pit_divisor_ : 0x10000;
            next_irq_ = c.icount + std::max<uint64_t>(1, uint64_t(div) * cfg_.cycles_per_ms * 1000 / 1193182);
            reschedule(c);
        }
        break;
    case 0x42:
        if (ch2_write_low_) { ch2_latch_ = v; ch2_write_low_ = false; }
        else { ch2_reload_ = uint16_t(ch2_latch_ | v << 8); ch2_write_low_ = true; ch2_frozen_ = 0; ch2_start_ = c.icount; }
        break;
    case 0x61:
        if ((v & 1) && !(port61_ & 1)) { ch2_running_ = true; ch2_start_ = c.icount; ch2_frozen_ = 0; }
        if (!(v & 1) && (port61_ & 1)) { ch2_frozen_ += (c.icount - ch2_start_); ch2_running_ = false; }
        port61_ = uint8_t((v & 0x0F) | 0x20);
        break;
    case 0x388: c.icount += cfg_.cycles_per_ms / 1000; opl_index_ = v; break;
    case 0x389: {
        c.icount += cfg_.cycles_per_ms / 1000;
        opl_regs_[opl_index_] = v;
        opl_.push_back({c.icount, opl_index_, v, irq_depth_});
        if (opl_index_ == 4) {
            const uint64_t us = c.icount * 1000 / cfg_.cycles_per_ms;
            if (v & 0x80) opl_status_ = 0;
            else {
                if ((v & 1) && !opl_t1_on_) opl_t1_start_ = us;
                if ((v & 2) && !opl_t2_on_) opl_t2_start_ = us;
                opl_t1_on_ = v & 1; opl_t2_on_ = v & 2;
            }
        }
        break;
    }
    default: break;
    }
}
}
