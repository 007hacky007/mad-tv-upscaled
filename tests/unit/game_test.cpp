// The translated game, run headless on the user's files.
#include "data/hash.h"
#include "data/image.h"
#include "data/rsc.h"
#include "data/rsc_codec.h"
#include "render/compositor.h"
#include "render/pack.h"
#include "render/provenance.h"
#include "game/game.h"
#include "support/corpus.h"
#include <doctest/doctest.h>
#include <fstream>
#include <map>
#include <set>
#include <nlohmann/json.hpp>
using namespace mtv;
namespace {
constexpr uint16_t kEnter = 0x1C0D, kSpace = 0x3920;
std::filesystem::path temp_user_dir(const std::filesystem::path& name) {
    auto p = std::filesystem::temp_directory_path() / "mtv-game-test" / name;
    std::filesystem::remove_all(p);
    return p;
}
std::string frame_hash(const game::Game& g) { const auto rgb = g.frame_rgb(); return sha256(std::as_bytes(std::span(rgb))); }
nlohmann::json reference() {
    std::ifstream f(test::source_dir() / "reference/runs/newgame/dosbox-frames.json");
    return nlohmann::json::parse(f);
}
// Runs until the frame hash equals `want`; returns the number of frames it took, or -1.
int run_until(game::Game& g, const std::string& want, int limit, rt::HostInput in = {}) {
    for (int i = 0; i < limit; ++i) {
        REQUIRE(g.tick(in));
        in.keys.clear();
        if (frame_hash(g) == want) return i;
    }
    return -1;
}
// Plays a script of {"frame", "keys", "mouse", "buttons"} events until frame `until` (absolute retrace count from
// `start`, as mtv-tool run-game counts them).
void play(game::Game& g, const nlohmann::json& events, int start, int until) {
    rt::HostInput in;
    size_t next = 0;
    while (next < events.size() && events[next]["frame"].get<int>() < start) {
        const auto& e = events[next++];
        if (e.contains("mouse")) { in.mouse_x = e["mouse"][0]; in.mouse_y = e["mouse"][1]; }
        if (e.contains("buttons")) in.buttons = e["buttons"];
    }
    for (int f = start; f < until; ++f) {
        in.keys.clear();
        while (next < events.size() && events[next]["frame"].get<int>() <= f) {
            const auto& e = events[next++];
            if (e.contains("keys")) for (const auto& k : e["keys"]) in.keys.push_back(k.get<uint16_t>());
            if (e.contains("mouse")) { in.mouse_x = e["mouse"][0]; in.mouse_y = e["mouse"][1]; }
            if (e.contains("buttons")) in.buttons = e["buttons"];
        }
        REQUIRE(g.tick(in));
    }
}
nlohmann::json fixture(const char* name) {
    std::ifstream f(test::source_dir() / "tests/fixtures" / name);
    return nlohmann::json::parse(f);
}
// Game-state fields a load restores. Not restored by the original's load: the walking figures (DS:7B42, set up
// again from the rooms), the clock (keeps running), the driver's timers and DS:8BDC (startup pointers, counters).
nlohmann::json saved_state(const StateDump& d) {
    nlohmann::json out;
    for (auto& [k, v] : d.items())
        if ((k.rfind("state_", 0) == 0 && k != "state_7b42" && k != "state_8bdc" && k != "state_7c82") || k == "game_day" || k == "player_index") out[k] = v;
    return out;
}
// The floor map DS:7C82 also marks where figures stand; those few cells follow the figures.
int floor_map_differences(const StateDump& a, const StateDump& b) {
    const std::string x = a["state_7c82"], y = b["state_7c82"];
    int n = 0;
    for (size_t i = 0; i + 1 < x.size(); i += 2) n += x.substr(i, 2) != y.substr(i, 2);
    return n;
}
void run(game::Game& g, int n, rt::HostInput in = {}) {
    for (int i = 0; i < n; ++i) { REQUIRE(g.tick(in)); in.keys.clear(); }
}
// Title, player select, then a click on "New game" (as reference/scripts/smoke-newgame.json). The reference frames
// are English; German runs follow the same timing.
void start_new_game(game::Game& g, bool english = true) {
    const auto ref = reference();
    if (english) REQUIRE(run_until(g, ref["title"], 600) >= 0);
    else run(g, 300);
    rt::HostInput enter; enter.keys = {kEnter};
    run(g, 300, enter);
    rt::HostInput space; space.keys = {kSpace};
    if (english) REQUIRE(run_until(g, ref["player_select"], 600, space) >= 0);
    else run(g, 300, space);
    rt::HostInput at; at.mouse_x = 63; at.mouse_y = 100;
    run(g, 20, at);
    at.buttons = 1; run(g, 5, at);
    at.buttons = 0; run(g, 1000, at);
}
}
TEST_SUITE("corpus.game") {
TEST_CASE("language_prompt_matches_dosbox") {
    game::Game g({test::corpus_dir(), temp_user_dir("prompt"), std::nullopt, &rt::static_logic()});
    CHECK(run_until(g, reference()["language_prompt"], 300) >= 0);
}
TEST_CASE("english_argument_skips_prompt_and_title_matches_dosbox") {
    game::Game g({test::corpus_dir(), temp_user_dir("title"), game::Language::English, &rt::static_logic()});
    CHECK(run_until(g, reference()["title"], 600) >= 0);
    CHECK(g.dump()["language"] == 1);
}
TEST_CASE("german_argument_selects_german") {
    game::Game g({test::corpus_dir(), temp_user_dir("german"), game::Language::German, &rt::static_logic()});
    run(g, 300);
    CHECK(g.dump()["language"] == 0);
}
TEST_CASE("new_game_starts_at_17_00_and_the_clock_runs") {
    // There is no copy protection in Mad TV (analysis/findings.md); a new game goes straight to the building.
    game::Game g({test::corpus_dir(), temp_user_dir("newgame"), game::Language::English, &rt::static_logic()});
    start_new_game(g);
    const auto d = g.dump();
    CHECK(d["game_day"] == 1);
    CHECK(d["game_hour"] == 17);
    const int minute = d["game_minute"];
    run(g, 600);
    CHECK(g.dump()["game_minute"].get<int>() > minute);
}
TEST_CASE("same_input_gives_same_frames_and_state") {
    game::Game a({test::corpus_dir(), temp_user_dir("det-a"), game::Language::English, &rt::static_logic()});
    game::Game b({test::corpus_dir(), temp_user_dir("det-b"), game::Language::English, &rt::static_logic()});
    start_new_game(a);
    start_new_game(b);
    CHECK(frame_hash(a) == frame_hash(b));
    CHECK(compare_dumps(a.dump(), b.dump()).empty());
}
TEST_CASE("each_language_reads_only_its_own_text_slots") {
    const std::set<uint16_t> german{199, 42, 207, 47, 45, 46, 49, 44}, english{253, 246, 252, 251, 250, 249, 248, 247};
    for (auto lang : {game::Language::English, game::Language::German}) {
        game::Game g({test::corpus_dir(), temp_user_dir("slots"), lang, &rt::static_logic()});
        start_new_game(g, lang == game::Language::English);
        CHECK(g.dump()["game_hour"] == 17);
        const auto read = g.slots_read();
        const auto& own = lang == game::Language::English ? english : german;
        const auto& other = lang == game::Language::English ? german : english;
        CHECK(read.count(lang == game::Language::English ? 253 : 199) == 1);
        for (uint16_t s : other) CHECK_MESSAGE(read.count(s) == 0, "slot " << s);
        size_t own_read = 0;
        for (uint16_t s : own) own_read += read.count(s);
        CHECK(own_read >= 1);
    }
}
TEST_CASE("switching_language_loads_the_strings_of_a_fresh_start") {
    game::Game de({test::corpus_dir(), temp_user_dir("switch-de"), game::Language::German, &rt::static_logic()});
    game::Game en({test::corpus_dir(), temp_user_dir("switch-en"), game::Language::English, &rt::static_logic()});
    run(de, 900); // the message table is loaded after the title
    run(en, 900);
    CHECK(de.message(1) != en.message(1));
    de.set_language(game::Language::English);
    CHECK(de.dump()["language"] == 1);
    for (int i = 0; i < 430; ++i) CHECK_MESSAGE(de.message(i) == en.message(i), "message " << i);
    de.set_language(game::Language::German);
    game::Game de2({test::corpus_dir(), temp_user_dir("switch-de2"), game::Language::German, &rt::static_logic()});
    run(de2, 900);
    for (int i = 0; i < 430; ++i) CHECK_MESSAGE(de.message(i) == de2.message(i), "message " << i);
}
TEST_CASE("language_switch_preserves_state_dump") {
    game::Game a({test::corpus_dir(), temp_user_dir("keep-a"), game::Language::English, &rt::static_logic()});
    game::Game b({test::corpus_dir(), temp_user_dir("keep-b"), game::Language::English, &rt::static_logic()});
    start_new_game(a);
    start_new_game(b);
    a.set_language(game::Language::German);
    run(a, 400);
    run(b, 400);
    a.set_language(game::Language::English);
    run(a, 400);
    run(b, 400);
    const auto diff = compare_dumps(a.dump(), b.dump());
    for (const auto& d : diff) MESSAGE(d);
    CHECK(diff.empty()); // text already on screen stays until the game redraws it
}
TEST_CASE("dump_reads_the_program_data_segment_at_every_tick") {
    // The CPU's DS at a retrace is whatever the running routine uses (blitters load the image segment).
    game::Game g({test::corpus_dir(), temp_user_dir("ds"), game::Language::English, &rt::static_logic()});
    int wrong = 0;
    for (int i = 0; i < 1200; ++i) {
        REQUIRE(g.tick({}));
        if (i > 200 && g.dump()["language"] != 1) ++wrong;
    }
    CHECK(wrong == 0);
}
TEST_CASE("loading_in_a_running_game_restores_the_saved_state") {
    const auto fx = fixture("save-game.json");
    const int saved = fx["saved_by"];
    game::Game g({test::corpus_dir(), temp_user_dir("saveload"), std::nullopt, &rt::static_logic()});
    play(g, fx["events"], 0, saved);
    const auto full_at_save = g.dump();
    const auto at_save = saved_state(full_at_save);
    // Let the day go on, then open the safe again and load the first slot.
    run(g, 3000);
    nlohmann::json load = nlohmann::json::array({
        {{"frame", saved + 3100}, {"mouse", {80, 30}}}, {{"frame", saved + 3120}, {"buttons", 1}}, {{"frame", saved + 3125}, {"buttons", 0}},
        {{"frame", saved + 3300}, {"mouse", {193, 75}}}, {{"frame", saved + 3320}, {"buttons", 1}}, {{"frame", saved + 3325}, {"buttons", 0}},
        {{"frame", saved + 3500}, {"mouse", {100, 28}}}, {{"frame", saved + 3520}, {"buttons", 1}}, {{"frame", saved + 3525}, {"buttons", 0}}});
    play(g, load, saved + 3000, saved + 4200);
    CHECK(floor_map_differences(full_at_save, g.dump()) <= 4);
    const auto diff = compare_dumps(at_save, saved_state(g.dump()));
    const auto now = saved_state(g.dump());
    for (const auto& d : diff) {
        const std::string k = d.substr(1), a = at_save[k], b = now[k];
        std::string where;
        for (size_t i = 0; i + 1 < a.size(); i += 2) if (a.substr(i, 2) != b.substr(i, 2)) where += std::to_string(i / 2) + ":" + a.substr(i, 2) + ">" + b.substr(i, 2) + " ";
        MESSAGE(d << " " << where);
    }
    CHECK(diff.empty());
}
TEST_CASE("save_survives_restart_and_loads_from_the_title") {
    const auto fx = fixture("save-game.json");
    const int saved = fx["saved_by"];
    const auto dir = temp_user_dir(std::filesystem::path(u8"Spielst\u00e4nde \u00e4\u00f6"));
    nlohmann::json at_save;
    StateDump full_at_save;
    {
        game::Game g({test::corpus_dir(), dir, std::nullopt, &rt::static_logic()});
        play(g, fx["events"], 0, saved);
        full_at_save = g.dump();
        at_save = saved_state(full_at_save);
    } // the window closes here; the game never exits
    game::Game g({test::corpus_dir(), dir, game::Language::English, &rt::static_logic()});
    REQUIRE(run_until(g, reference()["title"], 600) >= 0);
    rt::HostInput enter; enter.keys = {kEnter};
    run(g, 300, enter);
    rt::HostInput space; space.keys = {kSpace};
    REQUIRE(run_until(g, reference()["player_select"], 600, space) >= 0);
    CHECK_FALSE(compare_dumps(at_save, saved_state(g.dump())).empty()); // nothing loaded yet
    nlohmann::json load = nlohmann::json::array({
        {{"frame", 0}, {"mouse", {150, 100}}}, {{"frame", 20}, {"buttons", 1}}, {{"frame", 25}, {"buttons", 0}},
        {{"frame", 300}, {"mouse", {100, 28}}}, {{"frame", 320}, {"buttons", 1}}, {{"frame", 325}, {"buttons", 0}}});
    play(g, load, 0, 1200);
    CHECK(floor_map_differences(full_at_save, g.dump()) <= 4);
    const auto diff = compare_dumps(at_save, saved_state(g.dump()));
    const auto now = saved_state(g.dump());
    for (const auto& d : diff) {
        const std::string k = d.substr(1), a = at_save[k], b = now[k];
        std::string where;
        for (size_t i = 0; i + 1 < a.size(); i += 2) if (a.substr(i, 2) != b.substr(i, 2)) where += std::to_string(i / 2) + ":" + a.substr(i, 2) + ">" + b.substr(i, 2) + " ";
        MESSAGE(d << " " << where);
    }
    CHECK(diff.empty());
}
TEST_CASE("damaged_save_file_fails_cleanly_and_is_kept") {
    // The game keeps its saves inside its own SOURCE1.RSC; the port keeps that copy in the user directory.
    const auto dir = temp_user_dir("damaged");
    std::filesystem::create_directories(dir);
    auto bytes = test::read_corpus_file("SOURCE1.RSC");
    bytes.resize(bytes.size() / 2);
    std::ofstream(dir / "SOURCE1.RSC", std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    std::string error;
    try {
        game::Game g({test::corpus_dir(), dir, game::Language::English, &rt::static_logic()});
    } catch (const std::runtime_error& e) {
        error = e.what();
    }
    CHECK(error.find("SOURCE1.RSC") != std::string::npos);
    CHECK(std::filesystem::file_size(dir / "SOURCE1.RSC") == bytes.size());
}
TEST_CASE("title_music_matches_dosbox_registers_and_timing") {
    std::ifstream f(test::source_dir() / "reference/runs/title-music/dosbox-opl.json");
    const auto ref = nlohmann::json::parse(f);
    game::Game g({test::corpus_dir(), temp_user_dir("music"), game::Language::English, &rt::static_logic()});
    run(g, 2300);
    const auto& w = g.machine().opl_writes();
    size_t first = 0;
    while (first < w.size() && !(w[first].reg >= 0xB0 && w[first].reg <= 0xB8 && (w[first].value & 0x20))) ++first;
    REQUIRE(first + ref["pairs"].get<size_t>() <= w.size());
    std::vector<uint8_t> pairs;
    for (size_t i = first; i < first + ref["pairs"].get<size_t>(); ++i) { pairs.push_back(uint8_t(w[i].reg)); pairs.push_back(w[i].value); }
    CHECK(sha256(std::as_bytes(std::span(pairs))) == ref["sha256"].get<std::string>());
    std::vector<double> keyon;
    for (size_t i = first; i < w.size(); ++i)
        if (w[i].reg >= 0xB0 && w[i].reg <= 0xB8 && (w[i].value & 0x20)) keyon.push_back(double(w[i].icount - w[first].icount) / 8000.0);
    const auto want = ref["keyon_ms"];
    REQUIRE(keyon.size() >= want.size());
    int off = 0;
    for (size_t i = 0; i < want.size(); ++i)
        if (std::abs(keyon[i] - want[i].get<double>()) > 6) { if (off++ < 5) MESSAGE("key-on " << i << ": " << keyon[i] << " ms, DOSBox " << want[i].get<double>()); }
    CHECK(off == 0);
}
TEST_CASE("adlib_delay_calibration_matches_dosbox") {
    // The Palladix driver times 256 AdLib port reads with PIT channel 2 (gate on port 61h) and derives its
    // register-write delays from it (0xD14). DOSBox measures 3 and 15 loop counts.
    game::Game g({test::corpus_dir(), temp_user_dir("calib"), game::Language::English, &rt::static_logic()});
    run(g, 300);
    const auto& m = g.machine().mem;
    const uint32_t drv = uint32_t(m.r16(8 * 4 + 2)) * 16; // the driver's timer handler segment
    const uint16_t index_delay = m.r16(drv + 0xAA9), data_delay = m.r16(drv + 0xAAB);
    CHECK(index_delay >= 2);
    CHECK(index_delay <= 5);
    CHECK(data_delay >= 10);
    CHECK(data_delay <= 25);
}
TEST_CASE("timer_interrupts_fire_at_their_own_time_during_retrace_waits") {
    // The program waits for the retrace by polling port 3DAh; the runtime skips that wait, but a timer
    // interrupt due before the retrace must still fire at its own time, not in a burst at the retrace.
    game::Game g({test::corpus_dir(), temp_user_dir("irq"), game::Language::English, &rt::static_logic()});
    g.machine().record_irqs(true);
    run(g, 1500);
    const auto& t = g.machine().irq_times();
    REQUIRE(t.size() > 1000);
    uint64_t min_gap = UINT64_MAX;
    for (size_t i = 1; i < t.size(); ++i) min_gap = std::min(min_gap, t[i] - t[i - 1]);
    CHECK(double(min_gap) / 8000.0 > 2.3); // the driver's period is 4.69 ms
}
TEST_CASE("decoded_image_pixels_carry_their_source") {
    // Tags made at the image decoder's pixel store survive the blitters: a tagged screen pixel names the slot,
    // frame and pixel it was copied from, and that source pixel has the same colour index.
    game::Game g({test::corpus_dir(), temp_user_dir("tags"), game::Language::English, &rt::static_logic()});
    REQUIRE(run_until(g, reference()["title"], 600) >= 0);
    auto index = ResourceIndex::open_dir(test::corpus_dir());
    REQUIRE(index.has_value());
    std::map<uint16_t, std::vector<IndexedImage>> images;
    const auto& f = g.frame();
    int tagged = 0, matching = 0, nonzero = 0;
    std::set<uint16_t> slots;
    for (size_t i = 0; i < 64000; ++i) {
        nonzero += f.pixels[i] != 0;
        const auto ref = g.image_of(f.tags[i]);
        if (!ref) continue;
        ++tagged;
        slots.insert(ref->slot);
        auto it = images.find(ref->slot);
        if (it == images.end()) {
            auto e = decode_rsc_entry(index.value(), ref->slot);
            REQUIRE(e.has_value());
            it = images.emplace(ref->slot, decode_image(e.value()).value()).first;
        }
        REQUIRE(ref->frame < it->second.size());
        const auto& img = it->second[ref->frame];
        const auto [x, y] = render::tag_xy(f.tags[i]);
        REQUIRE(x < img.width);
        REQUIRE(y < img.height);
        matching += img.indices[size_t(y) * img.width + x] == f.pixels[i];
    }
    MESSAGE("tagged " << tagged << " of " << nonzero << " non-zero pixels, slots " << slots.size());
    CHECK(tagged > nonzero * 9 / 10);
    CHECK(matching == tagged);
}
TEST_CASE("every_image_on_screen_is_in_the_pack") {
    // Built with: cmake --build <build dir> --target mtv-pack (or MTV_PACK=<dir>).
    const std::filesystem::path dir = std::getenv("MTV_PACK") ? std::getenv("MTV_PACK") : MTV_PACK_DIR;
    auto pack = render::Pack::open(dir);
    REQUIRE_MESSAGE(pack.has_value(), "upscale pack missing at " << dir.string() << " (cmake --build <dir> --target mtv-pack)");
    const auto fx = fixture("save-game.json"); // new game, building, info board, office, safe
    game::Game g({test::corpus_dir(), temp_user_dir("pack"), std::nullopt, &rt::static_logic()});
    std::set<std::pair<uint16_t, uint16_t>> missing, seen;
    long tagged = 0, untagged = 0;
    nlohmann::json events = fx["events"];
    for (int until = 300; until <= fx["saved_by"].get<int>(); until += 300) {
        play(g, events, until - 300, until);
        const auto& f = g.frame();
        if (f.mode != 0x13) continue;
        for (size_t i = 0; i < 64000; ++i) {
            const auto ref = g.image_of(f.tags[i]);
            if (!ref) { untagged += f.pixels[i] != 0; continue; }
            ++tagged;
            seen.insert({ref->slot, ref->frame});
            if (!pack.value()->contains(*ref)) missing.insert({ref->slot, ref->frame});
        }
    }
    MESSAGE(seen.size() << " images seen; " << tagged << " tagged and " << untagged << " untagged non-zero pixels sampled");
    for (auto [slot, frame] : missing) MESSAGE("missing slot " << slot << " frame " << frame);
    CHECK(missing.empty());
    CHECK(seen.size() > 20);
}
TEST_CASE("graphics_toggle_preserves_state_dump") {
    // Drawing the upscaled picture reads the frame only; the game runs the same either way.
    const std::filesystem::path dir = std::getenv("MTV_PACK") ? std::getenv("MTV_PACK") : MTV_PACK_DIR;
    auto pack = render::Pack::open(dir);
    REQUIRE(pack.has_value());
    render::Compositor comp(pack.value().get());
    game::Game a({test::corpus_dir(), temp_user_dir("toggle-a"), game::Language::English, &rt::static_logic()});
    game::Game b({test::corpus_dir(), temp_user_dir("toggle-b"), game::Language::English, &rt::static_logic()});
    for (int i = 0; i < 900; ++i) {
        REQUIRE(a.tick({}));
        REQUIRE(b.tick({}));
        if ((i / 100) % 2 && a.frame().mode == 0x13) comp.update(a.frame().pixels, a.frame().tags, a.frame().dac, [&](render::Tag t) { return a.image_of(t); });
    }
    CHECK(compare_dumps(a.dump(), b.dump()).empty());
    CHECK(frame_hash(a) == frame_hash(b));
}
TEST_CASE("every_image_slot_is_recognised_at_the_decoder") {
    // The decoder entry names the slot from the packed bytes, including entries shorter than the 512-byte prefix.
    auto index = ResourceIndex::open_dir(test::corpus_dir());
    REQUIRE(index.has_value());
    game::ImageProvenance prov(test::corpus_dir());
    int images = 0;
    for (uint16_t slot : index.value().slots()) {
        auto e = decode_rsc_entry(index.value(), slot);
        if (!e.has_value() || e.value().kind != ResourceKind::Image) continue;
        ++images;
        auto bytes = index.value().get(slot).value();
        std::vector<uint8_t> mem(bytes.begin(), bytes.end());
        mem.resize(mem.size() + 600, 0xA5); // whatever follows the entry in memory
        const auto found = prov.slot_of(mem.data(), mem.size());
        REQUIRE_MESSAGE(found.has_value(), "slot " << slot);
        auto again = index.value().get(*found).value();
        CHECK_MESSAGE(std::equal(bytes.begin(), bytes.end(), again.begin(), again.end()), "slot " << slot << " taken for " << *found);
    }
    CHECK(images == 164);
}
TEST_CASE("an_abnormal_end_is_reported") {
    // Space at the language prompt can reach the game's hotkey screen before any data exists: random(0) divides
    // by zero and Turbo C ends the program with code 3 ("Divide error"). The port must say so.
    game::Game g({test::corpus_dir(), temp_user_dir("fault"), std::nullopt, &rt::static_logic()});
    CHECK_FALSE(g.failure().has_value());
    rt::HostInput in;
    bool running = true;
    for (int i = 0; i < 1500 && running; ++i) {
        in.keys.clear();
        if (i == 400) in.keys = {kSpace};
        running = g.tick(in);
    }
    REQUIRE_FALSE(running);
    const auto f = g.failure();
    REQUIRE(f.has_value());
    CHECK(f->find("3") != std::string::npos);
}
TEST_CASE("a_save_made_after_a_language_switch_loads_in_the_other_language") {
    const auto fx = fixture("save-game.json");
    const int saved = fx["saved_by"];
    const auto dir = temp_user_dir("switch-save");
    {
        game::Game g({test::corpus_dir(), dir, std::nullopt, &rt::static_logic()});
        play(g, fx["events"], 0, 8700);
        g.set_language(game::Language::German);
        play(g, fx["events"], 8700, saved);
        CHECK(g.text_pointers_consistent());
        // loading in the same session (started in English, now German) must keep the German table in use
        nlohmann::json load = nlohmann::json::array({
            {{"frame", saved + 100}, {"mouse", {80, 30}}}, {{"frame", saved + 120}, {"buttons", 1}}, {{"frame", saved + 125}, {"buttons", 0}},
            {{"frame", saved + 300}, {"mouse", {193, 75}}}, {{"frame", saved + 320}, {"buttons", 1}}, {{"frame", saved + 325}, {"buttons", 0}},
            {{"frame", saved + 500}, {"mouse", {100, 28}}}, {{"frame", saved + 520}, {"buttons", 1}}, {{"frame", saved + 525}, {"buttons", 0}}});
        play(g, load, saved, saved + 1200);
        CHECK(g.dump()["language"] == 0);
        CHECK(g.text_pointers_consistent());
    }
    game::Game g({test::corpus_dir(), dir, game::Language::German, &rt::static_logic()});
    run(g, 300);
    rt::HostInput enter; enter.keys = {kEnter};
    run(g, 300, enter);
    rt::HostInput space; space.keys = {kSpace};
    run(g, 300, space);
    nlohmann::json load = nlohmann::json::array({
        {{"frame", 0}, {"mouse", {150, 100}}}, {{"frame", 20}, {"buttons", 1}}, {{"frame", 25}, {"buttons", 0}},
        {{"frame", 300}, {"mouse", {100, 28}}}, {{"frame", 320}, {"buttons", 1}}, {{"frame", 325}, {"buttons", 0}}});
    play(g, load, 0, 1200);
    CHECK(g.dump()["game_hour"] == 17);
    CHECK(g.text_pointers_consistent());
}
}
TEST_SUITE("corpus.game") {
// The translated code comes only through the LogicTables passed in: without the driver modules, the first call into
// the music driver is untranslated code.
TEST_CASE("logic_tables_are_used") {
    const auto& full = rt::static_logic();
    const rt::LogicTables no_drivers{rt::kLogicAbi, full.build_id, full.functions, full.function_count, nullptr, 0};
    game::Game g({test::corpus_dir(), temp_user_dir("logic-no-drivers"), game::Language::English, &no_drivers});
    bool stopped = false;
    for (int i = 0; i < 600 && !stopped; ++i) stopped = !g.tick({});
    REQUIRE(stopped);
    REQUIRE(g.failure());
    CHECK(g.failure()->find("translated") != std::string::npos);
    game::Game ok({test::corpus_dir(), temp_user_dir("logic-full"), game::Language::English, &full});
    for (int i = 0; i < 600; ++i) REQUIRE(ok.tick({}));
}
}
TEST_SUITE("corpus.game") {
// The installed logic is unlocked from <user>/game again after an update, so saving must leave those files alone.
TEST_CASE("saving_leaves_pristine_files_untouched") {
    const auto game_dir = temp_user_dir("pristine-game"), user = temp_user_dir("pristine-user");
    std::filesystem::create_directories(game_dir);
    std::map<std::string, std::string> before;
    for (const auto& e : std::filesystem::directory_iterator(test::corpus_dir())) {
        if (!e.is_regular_file()) continue;
        std::filesystem::copy_file(e.path(), game_dir / e.path().filename());
        before[e.path().filename().string()] = sha256_file(game_dir / e.path().filename()).value();
    }
    const auto fx = fixture("save-game.json");
    {
        game::Game g({game_dir, user, std::nullopt, &rt::static_logic()});
        play(g, fx["events"], 0, fx["saved_by"]);
    }
    REQUIRE(std::filesystem::exists(user / "SOURCE1.RSC")); // the save went to the user folder
    for (const auto& [name, h] : before) CHECK_MESSAGE(sha256_file(game_dir / name).value() == h, name);
    CHECK(sha256_file(user / "SOURCE1.RSC").value() != before["SOURCE1.RSC"]);
}
}
TEST_SUITE("corpus.game") {
// Quitting from inside the game: the boss fires you, End on the player select, THE END, a key. The program returns
// to DOS through its exit handlers (one of them, at image offset 0x362F6, is reached only through a pointer).
TEST_CASE("quitting_through_the_boss_exits_cleanly") {
    const auto fx = fixture("quit-via-boss.json");
    const auto& events = fx["events"];
    game::Game g({test::corpus_dir(), temp_user_dir("quit"), game::Language::English, &rt::static_logic()});
    rt::HostInput in;
    size_t next = 0;
    int f = 0;
    bool running = true;
    for (; running && f < fx["exits_by"].get<int>(); ++f) {
        in.keys.clear();
        while (next < events.size() && events[next]["frame"].get<int>() <= f) {
            const auto& e = events[next++];
            if (e.contains("keys")) for (const auto& k : e["keys"]) in.keys.push_back(k.get<uint16_t>());
            if (e.contains("mouse")) { in.mouse_x = e["mouse"][0]; in.mouse_y = e["mouse"][1]; }
            if (e.contains("buttons")) in.buttons = e["buttons"];
        }
        running = g.tick(in);
    }
    CHECK_FALSE(running);
    CHECK_MESSAGE(!g.failure(), (g.failure() ? *g.failure() : std::string()));
}
}
