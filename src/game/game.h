#pragma once
// The translated Mad TV program on the modelled PC, one VGA retrace per tick.
#include "debug/state_dump.h"
#include "game/language.h"
#include "game/provenance.h"
#include "rt/dos.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace mtv::game {
struct GameConfig {
    std::filesystem::path game_dir;   // the user's original files (read only)
    std::filesystem::path user_dir;   // where the game's own writes go (saves)
    std::optional<Language> language; // passed as the game's own argument; nullopt shows the prompt
    const rt::LogicTables* logic = nullptr; // the translated program (required)
};
class Game {
public:
    explicit Game(const GameConfig& cfg); // throws std::runtime_error when the files cannot be used
    ~Game();
    // Runs to the next vertical retrace. Returns false once the program has exited.
    bool tick(const rt::HostInput& in);
    const rt::Frame& frame() const;
    std::vector<uint8_t> frame_rgb() const; // 320x200 RGB24 (black outside mode 13h)
    StateDump dump() const;                 // fields of analysis/evidence/state-layout.json
    std::optional<int> exit_code() const;
    // Why the program stopped, when it did not end normally (exit code other than 0, or a fault in the port).
    std::optional<std::string> failure() const;
    rt::DosMachine& machine() { return *machine_; }
    // Message `i` of the 430-string table at DS:8390 (code page 437 bytes).
    std::string message(int i) const;
    // Switches the running game to the other language between ticks: loads that language's message table into
    // the host area, repoints DS:8390 and the strings copied from it at start, and sets DS:055C. Rooms load their
    // own texts by DS:055C when they open. Throws std::runtime_error when the table cannot be loaded.
    void set_language(Language l);
    // True when the string pointers set up at start (DS:8BF7/8C0E/8C25) name messages 71/76/145 of the table in use.
    bool text_pointers_consistent() const;
    // The decoded image pixel a frame tag names, if any.
    std::optional<render::ImageRef> image_of(render::Tag t) const { return provenance_->image_of(t); }
    // RSC slots the program has read so far.
    std::set<uint16_t> slots_read() const;
private:
    uint16_t data_seg() const; // the program's DS (DGROUP)
    uint16_t rd16(uint16_t off) const;
    void wr16(uint16_t off, uint16_t v);
    uint32_t far_ptr(uint16_t off) const; // linear address of the far pointer stored at DS:off
    void point_derived_strings();
    std::optional<uint16_t> startup_language_;
    uint16_t data_seg_rel_ = 0;
    GameConfig cfg_;
    std::unique_ptr<ImageProvenance> provenance_;
    std::unique_ptr<rt::DosMachine> machine_;
    uint64_t ticks_ = 0;
};
}
