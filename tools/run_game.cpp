// mtv-tool run-game: run the translated game headless and save frames (for comparison with the reference).
#include "extract.h"
#include "data/lzexe.h"
#include "data/mz.h"
#include "audio/opl.h"
#include "game/provenance.h"
#include "render/compositor.h"
#include "rt/dos.h"
#include "mtv.h"
#include "stb_image_write.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include "rt/logic.h"
namespace mtv::tool {
int run_game_command(std::span<const std::string_view> args) {
    std::string data = "mad-tv", out = "work/run", script, tail;
    int frames = 300, every = 50;
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--data") data = args[i + 1];
        else if (args[i] == "--out") out = args[i + 1];
        else if (args[i] == "--frames") frames = std::stoi(std::string(args[i + 1]));
        else if (args[i] == "--every") every = std::stoi(std::string(args[i + 1]));
        else if (args[i] == "--script") script = args[i + 1];
        else if (args[i] == "--tail") tail = args[i + 1];
    }
    std::ifstream in(std::filesystem::path(data) / "MTV.EXE", std::ios::binary);
    std::vector<uint8_t> packed{std::istreambuf_iterator<char>(in), {}};
    auto exe = unlzexe91(packed);
    if (!exe.has_value()) { std::cerr << exe.error().reason << '\n'; return 1; }
    auto hdr = parse_mz(exe.value()).value().header;
    std::filesystem::create_directories(out);
    rt::DosMachine::Config cfg{data, std::filesystem::path(out) / "user", tail, 8000};
    rt::DosMachine m(cfg, rt::static_logic(), uint32_t(hdr.cs) * 16 + hdr.ip, exe.value());
    m.log = [](const std::string& s) { std::cerr << s << '\n'; };
    // MTV_PACK=<dir>: also save the upscaled picture (u%05d.png, half size) next to each saved frame.
    std::unique_ptr<render::Pack> pack;
    std::unique_ptr<game::ImageProvenance> prov;
    std::unique_ptr<render::Compositor> comp;
    if (const char* p = std::getenv("MTV_PACK")) {
        auto opened = render::Pack::open(p);
        if (!opened.has_value()) { std::cerr << "pack: " << opened.error().reason << '\n'; return 1; }
        pack = std::move(opened.value());
        prov = std::make_unique<game::ImageProvenance>(data);
        comp = std::make_unique<render::Compositor>(pack.get());
        m.on_hook = [pv = prov.get()](rt::Cpu& c, uint32_t off) { if (off == game::ImageProvenance::kDecoderEntry) pv->decoder_entry(c); };
        m.on_store_tag = [pv = prov.get()](rt::Cpu& c, uint32_t off) { return off == game::ImageProvenance::kPixelStore ? pv->pixel_tag(c) : 0; };
    }
    // optional input script: JSON list of {"frame": n, "keys": [bios codes], "mouse": [x, y], "buttons": b}
    nlohmann::json events = nlohmann::json::array();
    if (!script.empty()) { std::ifstream f(script); events = nlohmann::json::parse(f); }
    size_t next = 0;
    rt::HostInput input;
    for (int f = 0; f < frames; ++f) {
        input.keys.clear();
        while (next < events.size() && events[next]["frame"].get<int>() <= f) {
            auto& e = events[next++];
            if (e.contains("keys")) for (auto k : e["keys"]) input.keys.push_back(k.get<uint16_t>());
            if (e.contains("mouse")) { input.mouse_x = e["mouse"][0]; input.mouse_y = e["mouse"][1]; }
            if (e.contains("buttons")) input.buttons = e["buttons"];
        }
        if (!m.run_frame(input)) {
            std::cerr << "program exited with " << *m.exit_code() << " at frame " << f << '\n';
            if (std::getenv("MTV_DUMP")) {
                const auto& c = m.cpu;
                std::fprintf(stderr, "AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X BP=%04X SP=%04X ES=%04X CS=%04X SS=%04X DS=%04X\n",
                             c.r[0], c.r[3], c.r[1], c.r[2], c.r[6], c.r[7], c.r[5], c.r[4], c.s[0], c.s[1], c.s[2], c.s[3]);
                for (int i = 0; i < 64; ++i) std::fprintf(stderr, "%02X ", m.mem.b[0x8000 + i]);
                std::fprintf(stderr, "\nPSP2C=%04X DS:75=%04X DS:77=%04X\n", m.mem.r16(0x8140 + 0x2C), m.mem.r16(0x39d40 + 0x8240 + 0x75), m.mem.r16(0x39d40 + 0x8240 + 0x77));
            }
            break;
        }
        const auto& fr = m.frame();
        if (const char* d = std::getenv("MTV_MEMDUMP"); d && f % std::atoi(d) == 0) {
            char name[32]; std::snprintf(name, sizeof name, "mem%05d.bin", f);
            std::ofstream(std::filesystem::path(out) / name, std::ios::binary).write(reinterpret_cast<const char*>(m.mem.b.data()), 0x100000);
        }
        if (const char* d = std::getenv("MTV_DSDUMP"); d && f % std::atoi(d) == 0) {
            char name[32]; std::snprintf(name, sizeof name, "ds%05d.bin", f);
            std::ofstream(std::filesystem::path(out) / name, std::ios::binary).write(reinterpret_cast<const char*>(&m.mem.b[uint32_t(m.load_seg + 0x39D4) * 16]), 65536); // DGROUP
        }
        if (f % every == 0 && fr.mode == 0x13) {
            std::vector<uint8_t> rgb(64000 * 3);
            for (size_t i = 0; i < 64000; ++i) for (int k = 0; k < 3; ++k) { uint8_t v = fr.dac[fr.pixels[i]][size_t(k)]; rgb[i * 3 + size_t(k)] = uint8_t(v << 2 | v >> 4); }
            char name[32]; std::snprintf(name, sizeof name, "f%05d.png", f);
            stbi_write_png((std::filesystem::path(out) / name).string().c_str(), 320, 200, 3, rgb.data(), 320 * 3);
        }
        if (std::getenv("MTV_TAGMASK") && fr.mode == 0x13 && f % every == 0 && fr.tags.size() == 64000) {
            // untagged non-zero pixels in red over a dimmed frame
            std::vector<uint8_t> rgb(64000 * 3);
            for (size_t i = 0; i < 64000; ++i) {
                const bool untagged = !render::tag_valid(fr.tags[i]) && fr.pixels[i];
                for (int k = 0; k < 3; ++k) { const uint8_t v = fr.dac[fr.pixels[i]][size_t(k)]; rgb[i * 3 + size_t(k)] = untagged ? (k == 0 ? 255 : 0) : uint8_t(v); }
            }
            char name[32]; std::snprintf(name, sizeof name, "t%05d.png", f);
            stbi_write_png((std::filesystem::path(out) / name).string().c_str(), 320, 200, 3, rgb.data(), 320 * 3);
        }
        if (comp && fr.mode == 0x13) {
            comp->update(fr.pixels, fr.tags, fr.dac, [&](render::Tag t) { return prov->image_of(t); });
            if (f % every == 0) {
                const auto& cv = comp->canvas();
                std::vector<uint8_t> half(960 * 600 * 4);
                for (int y = 0; y < 600; ++y)
                    for (int x = 0; x < 960; ++x) std::memcpy(&half[(size_t(y) * 960 + x) * 4], &cv[size_t(y * 2) * 1920 + x * 2], 4);
                char name[32]; std::snprintf(name, sizeof name, "u%05d.png", f);
                stbi_write_png((std::filesystem::path(out) / name).string().c_str(), 960, 600, 4, half.data(), 960 * 4);
            }
        }
    }
    if (const char* wav = std::getenv("MTV_WAV")) {
        // The AdLib output of the whole run as 16-bit mono WAV at the chip's rate.
        audio::OplStream st(8000);
        std::vector<int16_t> pcm;
        st.render(0, m.cpu.icount, m.opl_writes(), pcm);
        std::ofstream o(wav, std::ios::binary);
        auto u32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        o.write("RIFF", 4); u32(36 + bytes); o.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
        u32(audio::Opl::kSampleRate); u32(audio::Opl::kSampleRate * 2); u16(2); u16(16); o.write("data", 4); u32(bytes);
        o.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
    if (std::getenv("MTV_OPL")) {
        std::ofstream o(std::filesystem::path(out) / "opl.txt");
        for (const auto& w : m.opl_writes()) o << w.icount << ' ' << std::hex << w.reg << ' ' << int(w.value) << std::dec << ' ' << int(w.irq_depth) << '\n';
    }
    std::cerr << "opl writes " << m.opl_writes().size() << '\n';
    std::cerr << "ran " << m.frame().index << " retraces, " << m.cpu.icount << " instructions, peak segment " << std::hex << m.peak_segment() << std::dec << '\n';
    return 0;
}
}
