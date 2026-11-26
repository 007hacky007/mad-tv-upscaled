#include "game/game.h"
#include "data/lzexe.h"
#include "data/mz.h"
#include "data/rsc.h"
#include "data/rsc_codec.h"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
namespace mtv::game {
extern const char* const kStateLayoutJson;
namespace {
std::vector<uint8_t> read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
}
Game::Game(const GameConfig& cfg) : cfg_(cfg) {
    // The game writes its saves into its own SOURCE1.RSC; the port keeps that copy in the user directory, where it
    // shadows the original. A damaged copy would break the game, so refuse to start and leave the file for repair.
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(cfg.user_dir, ec)) {
        std::string name = e.path().filename().string();
        for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (name.size() < 4 || name.substr(name.size() - 4) != ".RSC") continue;
        auto a = RscArchive::open(read_file(e.path()), name);
        if (!a.has_value())
            throw std::runtime_error(std::string("saved games are damaged: ") + reinterpret_cast<const char*>(e.path().u8string().c_str()) + " (" + a.error().resource + ": " + a.error().reason + ")");
    }
    auto exe = unlzexe91(read_file(cfg.game_dir / "MTV.EXE"));
    if (!exe.has_value()) throw std::runtime_error("MTV.EXE: " + exe.error().reason);
    auto mz = parse_mz(exe.value());
    if (!mz.has_value()) throw std::runtime_error("MTV.EXE: " + mz.error().reason);
    const auto& h = mz.value().header;
    std::string tail;
    if (cfg.language) tail = *cfg.language == Language::English ? " e" : " d";
    rt::DosMachine::Config mc{cfg.game_dir, cfg.user_dir, tail, 8000};
    // The data segment starts 4 bytes before Turbo C's copyright string (as tools/reference/dump_state.py).
    static constexpr std::string_view kSignature = "Turbo-C - Copyright (c) 1988";
    const auto& bytes = exe.value();
    const size_t image = size_t(h.header_paragraphs) * 16;
    auto it = std::search(bytes.begin() + std::ptrdiff_t(image), bytes.end(), kSignature.begin(), kSignature.end());
    const size_t at = size_t(it - bytes.begin()) - image;
    if (it == bytes.end() || at < 4 || (at - 4) % 16) throw std::runtime_error("MTV.EXE: data segment not found");
    data_seg_rel_ = uint16_t((at - 4) / 16);
    if (!cfg.logic || cfg.logic->abi != rt::kLogicAbi) throw std::runtime_error("the translated program is missing");
    machine_ = std::make_unique<rt::DosMachine>(mc, *cfg.logic, uint32_t(h.cs) * 16 + h.ip, std::move(exe.value()));
    provenance_ = std::make_unique<ImageProvenance>(cfg.game_dir);
    machine_->on_hook = [p = provenance_.get()](rt::Cpu& c, uint32_t off) { if (off == ImageProvenance::kDecoderEntry) p->decoder_entry(c); };
    machine_->on_store_tag = [p = provenance_.get()](rt::Cpu& c, uint32_t off) { return off == ImageProvenance::kPixelStore ? p->pixel_tag(c) : 0; };
}
Game::~Game() = default;
bool Game::tick(const rt::HostInput& in) { ++ticks_; return machine_->run_frame(in); }
const rt::Frame& Game::frame() const { return machine_->frame(); }
std::optional<int> Game::exit_code() const { return machine_->exit_code(); }
std::optional<std::string> Game::failure() const {
    const auto code = machine_->exit_code();
    if (!code || *code == 0) return std::nullopt;
    if (!machine_->error().empty()) return "the program stopped: " + machine_->error();
    if (*code == 3) return std::string("the program ended with \"Divide error\" (exit code 3)");
    return "the program ended with exit code " + std::to_string(*code);
}
std::vector<uint8_t> Game::frame_rgb() const {
    const auto& f = frame();
    std::vector<uint8_t> rgb(64000 * 3, 0);
    if (f.mode != 0x13 || f.pixels.size() != 64000) return rgb;
    for (size_t i = 0; i < 64000; ++i)
        for (size_t k = 0; k < 3; ++k) { const uint8_t v = f.dac[f.pixels[i]][k]; rgb[i * 3 + k] = uint8_t(v << 2 | v >> 4); }
    return rgb;
}
StateDump Game::dump() const {
    static const auto layout = nlohmann::json::parse(kStateLayoutJson);
    const auto& m = machine_->mem;
    const uint32_t ds = uint32_t(data_seg()) * 16;
    auto rd = [&](uint32_t off, int size, bool sign) -> int64_t {
        uint64_t v = 0;
        for (int i = 0; i < size; ++i) v |= uint64_t(m.b[(ds + ((off + uint32_t(i)) & 0xFFFF)) & 0xFFFFF]) << (8 * i);
        if (sign && (v >> (size * 8 - 1)) & 1) return int64_t(v) - (int64_t(1) << (size * 8));
        return int64_t(v);
    };
    StateDump out;
    out["tick"] = ticks_;
    for (const auto& f : layout["fields"]) {
        const uint32_t off = uint32_t(std::stoul(f["offset"].get<std::string>(), nullptr, 16));
        const std::string type = f["type"];
        if (type == "bytes") {
            static const char* hex = "0123456789abcdef";
            std::string s;
            for (int i = 0; i < f["size"].get<int>(); ++i) { auto b = uint8_t(rd(off + uint32_t(i), 1, false)); s += hex[b >> 4]; s += hex[b & 15]; }
            out[f["name"].get<std::string>()] = s;
            continue;
        }
        const int size = type.back() == '8' ? 1 : type.substr(type.size() - 2) == "16" ? 2 : 4;
        const bool sign = type[0] == 'i';
        if (f.contains("count")) {
            auto arr = nlohmann::json::array();
            for (int i = 0; i < f["count"].get<int>(); ++i) arr.push_back(rd(off + uint32_t(i * size), size, sign));
            out[f["name"].get<std::string>()] = arr;
        } else {
            out[f["name"].get<std::string>()] = rd(off, size, sign);
        }
    }
    return out;
}
}

namespace mtv::game {
namespace {
constexpr uint16_t kLanguage = 0x55C, kLanguageTable = 0x55E, kMessages = 0x8390;
constexpr int kMessageCount = 430;
// Strings FUN_1054_0a0b (0x10F4B) copies or points to after loading the table: {buffer ptr at, message, bytes}
// copied with FUN_1e35_088e, and {pointer at, message} stored directly.
struct Copied { uint16_t buffer_ptr; int message; int bytes; };
constexpr Copied kCopied[] = {{0x8BF3, 63, 7}, {0x8C0A, 67, 6}, {0x8C21, 69, 10}};
struct Pointed { uint16_t ptr; int message; };
constexpr Pointed kPointed[] = {{0x8BF7, 71}, {0x8C0E, 76}, {0x8C25, 145}};
}
uint16_t Game::data_seg() const { return uint16_t(machine_->load_seg + data_seg_rel_); }
uint16_t Game::rd16(uint16_t off) const { return machine_->mem.r16(rt::Memory::lin(data_seg(), off)); }
void Game::wr16(uint16_t off, uint16_t v) { machine_->mem.w16(rt::Memory::lin(data_seg(), off), v); }
uint32_t Game::far_ptr(uint16_t off) const { return rt::Memory::lin(rd16(uint16_t(off + 2)), rd16(off)); }
std::string Game::message(int i) const {
    if (i < 0 || i >= kMessageCount) return {};
    std::string out;
    for (uint32_t a = far_ptr(uint16_t(kMessages + i * 4)); out.size() < 4096; ++a) {
        const uint8_t b = machine_->mem.b[a & 0xFFFFF];
        if (!b) break;
        out += char(b);
    }
    return out;
}
void Game::set_language(Language l) {
    const uint16_t want = uint16_t(l);
    const uint16_t now = rd16(kLanguage);
    if (now == want) return;
    if (now > 1) throw std::runtime_error("the game has not chosen a language yet");
    if (!startup_language_) startup_language_ = now;
    auto& m = machine_->mem;
    if (want == *startup_language_) {
        // Back to the table the game loaded itself (DS:8BAE, never freed): rebuild exactly what 0x10F4B built.
        const uint16_t seg = rd16(0x8BB0);
        uint16_t off = rd16(0x8BAE);
        for (int i = 0; i < kMessageCount; ++i) {
            wr16(uint16_t(kMessages + i * 4), off);
            wr16(uint16_t(kMessages + i * 4 + 2), seg);
            while (m.b[rt::Memory::lin(seg, off)]) ++off;
            ++off;
        }
        point_derived_strings();
        wr16(kLanguage, want);
        return;
    }
    auto index = ResourceIndex::open_dir(cfg_.game_dir);
    if (!index.has_value()) throw std::runtime_error(index.error().reason);
    const uint16_t slot = rd16(uint16_t(kLanguageTable + want * 16));
    auto entry = decode_rsc_entry(index.value(), slot);
    if (!entry.has_value()) throw std::runtime_error("text slot " + std::to_string(slot) + ": " + entry.error().reason);
    const auto& text = entry.value().bytes;
    if (text.size() > 0x8000) throw std::runtime_error("text slot " + std::to_string(slot) + " does not fit the host area");
    std::vector<uint16_t> offsets;
    for (size_t pos = 0; offsets.size() < size_t(kMessageCount); ++pos) {
        offsets.push_back(uint16_t(pos));
        while (pos < text.size() && text[pos]) ++pos;
        if (pos >= text.size()) throw std::runtime_error("text slot " + std::to_string(slot) + " has fewer than 430 messages");
    }
    const uint32_t base = uint32_t(rt::kHostSegment) * 16;
    for (size_t i = 0; i < text.size(); ++i) m.w8(base + uint32_t(i), text[i]);
    for (int i = 0; i < kMessageCount; ++i) {
        wr16(uint16_t(kMessages + i * 4), offsets[size_t(i)]);
        wr16(uint16_t(kMessages + i * 4 + 2), rt::kHostSegment);
    }
    point_derived_strings();
    wr16(kLanguage, want);
}
void Game::point_derived_strings() {
    auto& m = machine_->mem;
    for (const auto& c : kCopied) {
        const uint32_t dst = far_ptr(c.buffer_ptr), src = far_ptr(uint16_t(kMessages + c.message * 4));
        for (int i = 0; i < c.bytes; ++i) m.w8((dst + uint32_t(i)) & 0xFFFFF, m.b[(src + uint32_t(i)) & 0xFFFFF]);
    }
    for (const auto& p : kPointed) {
        wr16(p.ptr, rd16(uint16_t(kMessages + p.message * 4)));
        wr16(uint16_t(p.ptr + 2), rd16(uint16_t(kMessages + p.message * 4 + 2)));
    }
}
std::set<uint16_t> Game::slots_read() const {
    std::set<uint16_t> out;
    std::map<std::string, std::vector<RscEntry>> tables;
    for (const auto& e : std::filesystem::directory_iterator(cfg_.game_dir)) {
        std::string name = e.path().filename().string();
        for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (name.size() < 4 || name.substr(name.size() - 4) != ".RSC") continue;
        auto a = RscArchive::open(read_file(e.path()), name);
        if (a.has_value()) tables[name].assign(a.value().entries().begin(), a.value().entries().end());
    }
    for (const auto& r : machine_->file_reads()) {
        auto it = tables.find(r.name);
        if (it == tables.end()) continue;
        for (const auto& e : it->second)
            if (r.offset < e.offset + e.size && e.offset < r.offset + r.size) out.insert(e.slot);
    }
    return out;
}
}

namespace mtv::game {
bool Game::text_pointers_consistent() const {
    for (const auto& p : kPointed)
        if (rd16(p.ptr) != rd16(uint16_t(kMessages + p.message * 4)) || rd16(uint16_t(p.ptr + 2)) != rd16(uint16_t(kMessages + p.message * 4 + 2))) return false;
    return true;
}
}
