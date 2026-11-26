#pragma once
// A small model of the PC and DOS that the translated Mad TV program runs on: memory and PSP, DOS files and
// memory blocks, BIOS video/keyboard/mouse/timer services, VGA retrace and palette ports, the PIT driving the
// Palladix timer handler, and AdLib register writes. Time is virtual: `cycles_per_ms` instructions per millisecond.
#include "rt/blobs.h"
#include "rt/logic.h"
#include "rt/machine.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>
namespace mtv::rt {
struct ProgramExit { int code; };
// Writes `bytes` to <path>.tmp, checks it, then renames it over `path`; on any failure `path` is left as it was.
bool replace_file(const std::filesystem::path& path, std::span<const uint8_t> bytes);
struct OplWrite { uint64_t icount; uint16_t reg; uint8_t value; uint8_t irq_depth = 0; };
struct FileRead { std::string name; uint32_t offset, size; }; // name: upper-case file name without directory
// Conventional memory above this segment is kept from DOS for the host (32 KB, e.g. a second string table).
inline constexpr uint16_t kHostSegment = 0x9800;
struct HostInput {
    int mouse_x = 160, mouse_y = 100; // logical 320x200
    uint8_t buttons = 0;              // bit 0 left, bit 1 right
    std::vector<uint16_t> keys;       // BIOS keystrokes (scan << 8 | ascii) delivered at this frame
};
struct Frame {
    uint8_t mode = 3;                     // BIOS video mode
    std::vector<uint8_t> pixels;          // mode 13h: 320x200 indices
    std::vector<Tag> tags;                // pixel-source tags for the upscaler
    std::array<std::array<uint8_t, 3>, 256> dac{}; // 6-bit VGA DAC
    uint64_t index = 0;                   // retrace count
};
class DosMachine : public Machine {
public:
    struct Config {
        std::filesystem::path game_dir;   // original files (read only)
        std::filesystem::path user_dir;   // files the game writes (copy on write)
        std::string command_tail;         // e.g. " e" to choose English
        uint32_t cycles_per_ms = 8000;
    };
    explicit DosMachine(Config cfg, const LogicTables& logic, uint32_t entry_image_offset, std::vector<uint8_t> exe);
    ~DosMachine() override;
    // Runs the program until it reaches the next vertical retrace (or exits). Returns false once it has exited.
    bool run_frame(const HostInput& in);
    const Frame& frame() const { return frame_; }
    std::optional<int> exit_code() const { return exit_code_; }
    const std::string& error() const { return error_; } // set when translated code stopped with a fault
    const std::vector<OplWrite>& opl_writes() const { return opl_; }
    // Highest paragraph the program's DOS memory blocks have reached.
    uint32_t peak_segment() const { return peak_seg_; }
    const std::vector<FileRead>& file_reads() const { return reads_; }
    // Instruction counts at which timer interrupts were delivered to the program (when recording is on).
    void record_irqs(bool on) { record_irqs_ = on; }
    const std::vector<uint64_t>& irq_times() const { return irq_times_; }
    void clear_opl_writes() { opl_.clear(); }
    std::function<void(const std::string&)> log;
    Memory mem;
    Cpu cpu;
    // Machine
    void interrupt(Cpu& c, uint8_t n, uint16_t return_ip) override;
    uint8_t in8(Cpu& c, uint16_t port) override;
    void out8(Cpu& c, uint16_t port, uint8_t v) override;
    void events(Cpu& c) override;
    void call_near(Cpu& c, uint16_t ip) override;
    void call_far(Cpu& c, uint16_t seg, uint16_t off) override;
    void jump_far(Cpu& c, uint16_t seg, uint16_t off) override;
    void jump_near(Cpu& c, uint16_t ip) override;
    void hook(Cpu& c, uint32_t image_offset) override { if (on_hook) on_hook(c, image_offset); }
    Tag store_tag(Cpu& c, uint32_t image_offset) override { return on_store_tag ? on_store_tag(c, image_offset) : 0; }
    std::function<void(Cpu&, uint32_t)> on_hook;
    std::function<Tag(Cpu&, uint32_t)> on_store_tag;
private:
    struct OpenFile { std::filesystem::path path; std::vector<uint8_t> data; uint32_t pos = 0; bool dirty = false; };
    void load_program(const std::vector<uint8_t>& exe);
    Fn lookup(uint16_t seg, uint16_t off);
    void dos(Cpu& c);
    void video(Cpu& c);
    void keyboard(Cpu& c);
    void mouse(Cpu& c);
    void bios_tick(Cpu& c);
    void deliver(Cpu& c, uint8_t n);
    void yield(Cpu& c);
    void reschedule(Cpu& c);
    std::filesystem::path resolve(const std::string& dos_name, bool for_write);
    std::string read_string(uint16_t seg, uint16_t off);
    void set_carry(Cpu& c, bool error, uint16_t ax = 0);
    uint64_t frame_period() const { return uint64_t(cfg_.cycles_per_ms) * 14268 / 1000; }
    Config cfg_;
    LogicTables logic_;
    std::unordered_map<uint32_t, Fn> static_fns_;
    uint32_t entry_;
    std::vector<uint8_t> exe_;
    // DOS state
    struct Block { uint16_t seg, paras; bool used; };
    std::vector<Block> blocks_;
    std::map<uint16_t, OpenFile> files_;
    uint16_t dta_seg_ = 0, dta_off_ = 0x80, psp_ = 0;
    std::vector<std::filesystem::path> find_results_; size_t find_pos_ = 0;
    // devices
    uint8_t video_mode_ = 3;
    std::array<std::array<uint8_t, 3>, 256> dac_{};
    uint8_t dac_write_ = 0, dac_read_ = 0, dac_comp_ = 0;
    uint16_t pit_divisor_ = 0, pit_latch_ = 0; bool pit_low_ = true;
    // PIT channel 2 (gated by port 61h bit 0): drivers use it as a stopwatch
    uint16_t ch2_reload_ = 0, ch2_latch_ = 0; bool ch2_write_low_ = true, ch2_read_low_ = true;
    uint8_t port61_ = 0x20; uint64_t ch2_start_ = 0, ch2_frozen_ = 0; bool ch2_running_ = false;
    uint16_t ch2_count(const Cpu& c) const;
    uint64_t next_irq_ = 0, next_frame_ = 0, frames_ = 0;
    uint8_t opl_index_ = 0;
    std::array<uint8_t, 256> opl_regs_{};
    uint64_t opl_t1_start_ = 0, opl_t2_start_ = 0; bool opl_t1_on_ = false, opl_t2_on_ = false; uint8_t opl_status_ = 0;
    std::vector<OplWrite> opl_;
    uint32_t peak_seg_ = 0;
    uint8_t irq_depth_ = 0;
    bool record_irqs_ = false;
    std::vector<uint64_t> irq_times_;
    std::vector<FileRead> reads_;
    HostInput input_;
    std::deque<uint16_t> keys_;
    int mouse_min_x_ = 0, mouse_max_x_ = 639, mouse_min_y_ = 0, mouse_max_y_ = 199;
    int mouse_x_ = 0, mouse_y_ = 0; uint8_t mouse_buttons_ = 0;
    Frame frame_;
    std::optional<int> exit_code_;
    // program thread handshake
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool host_turn_ = true, started_ = false, stop_ = false;
    std::string error_;
};
}
