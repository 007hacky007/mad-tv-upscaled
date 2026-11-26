#include "platform/app.h"
#include "platform/import.h"
#include "platform/input.h"
#include "platform/install.h"
#include "platform/pack_job.h"
#include "platform/logic_loader.h"
#include "platform/settings.h"
#include "platform/shortcuts.h"
#include "platform/ticker.h"
#include "render/compositor.h"
#include "render/gpu.h"
#include "render/pack.h"
#include "ui/options.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include "intro/intro.h"
#ifdef MTV_HAVE_GAME
#include "audio/opl.h"
#include "audio/pacer.h"
#include "game/game.h"
#endif
namespace mtv {
namespace {
struct NoFrames : FrameSource {
    Palette pal;
    std::optional<IndexedImage> frame(ResourceId, uint16_t) const override { return std::nullopt; }
    const Palette& palette() const override { return pal; }
    const Font* font() const override { return nullptr; }
};
std::filesystem::path utf8_path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::filesystem::path pref_dir() {
    // MTV_USER_DIR replaces the per-user folder (tests and side-by-side installs).
    if (const char* over = std::getenv("MTV_USER_DIR"); over && *over) return utf8_path(over);
    char* base = SDL_GetPrefPath("", "MadTV");
    std::filesystem::path p = base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
    SDL_free(base);
    return p;
}
}
int run_app(const AppOptions& options) {
    if (!options.import_dir.empty()) {
        auto r = import_game_data(utf8_path(options.import_dir), pref_dir());
        if (!r.has_value()) { std::cerr << "import failed: " << r.error().reason << '\n'; return 1; }
        std::cout << "imported " << r.value().files.size() << " files" << (r.value().pack ? " and the upscale pack" : "") << " into "
                  << reinterpret_cast<const char*>(pref_dir().u8string().c_str()) << '\n';
        return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    const auto settings_path = default_settings_path();
    Settings settings;
    if (auto s = load_settings(settings_path); s.has_value()) settings = s.value();
    else std::cerr << "settings: " << s.error().reason << " (using defaults)\n";
    std::string data_dir = !options.data_dir.empty() ? options.data_dir : settings.data_dir;
    if (data_dir.empty() && std::filesystem::exists(pref_dir() / "game" / "MTV.EXE"))
        data_dir = reinterpret_cast<const char*>((pref_dir() / "game").u8string().c_str());

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (settings.fullscreen && !options.smoke_test) flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* window = SDL_CreateWindow("Mad TV", 1280, 960, flags);
    if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1; }
    NoFrames no_frames;
    auto created = Renderer::create(window, no_frames);
    if (!created.has_value()) { std::cerr << created.error().reason << '\n'; SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    auto renderer = std::move(created.value());
    SDL_SetRenderVSync(SDL_GetRenderer(window), 1);

    if (data_dir.empty() && !options.smoke_test) {
        // First start: ask for the folder with the original files and copy them (and a pack next to them).
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose folder"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
        const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, window, "Mad TV",
                                     "Mad TV needs the original game files (MTV.EXE, SOURCE1.RSC, SOURCE2.RSC, INTRO.EXE, MCV.BIN, DATAV.PCK). "
                                     "Choose the folder that holds them; they are copied once.", 2, buttons, nullptr};
        int pressed = 0;
        while (data_dir.empty() && SDL_ShowMessageBox(&box, &pressed) && pressed == 1) {
            struct Pick { bool done = false; std::string path; } pick;
            SDL_ShowOpenFolderDialog([](void* u, const char* const* list, int) {
                auto* p = static_cast<Pick*>(u);
                if (list && list[0]) p->path = list[0];
                p->done = true;
            }, &pick, window, nullptr, false);
            while (!pick.done) { SDL_Event ev; SDL_WaitEventTimeout(&ev, 50); }
            if (pick.path.empty()) continue;
            auto r = import_game_data(utf8_path(pick.path), pref_dir());
            if (!r.has_value()) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mad TV", ("Import failed: " + r.error().reason).c_str(), window);
                continue;
            }
            data_dir = reinterpret_cast<const char*>((pref_dir() / "game").u8string().c_str());
            settings.data_dir = data_dir;
            if (auto w = save_settings(settings, settings_path); !w.has_value()) std::cerr << "settings: " << w.error().reason << '\n';
        }
    }
#ifdef MTV_HAVE_GAME
    // The translated program. A package carries it encrypted (logic.bin): it is unlocked once with the key from the
    // imported files into the user folder. A build tree has the plain mtv-logic library next to the executable.
    std::unique_ptr<LoadedLogic> logic;
    std::unique_ptr<game::Game> game;
    std::string problem = "The original Mad TV files were not found. Start with --data <folder containing MTV.EXE>.";
    if (!data_dir.empty()) {
        const char* base_c = SDL_GetBasePath();
        const auto base = utf8_path(base_c ? base_c : "");
        std::error_code ec;
        Result<std::unique_ptr<LoadedLogic>> l = DataError{};
        if (std::filesystem::exists(base / "logic.bin", ec)) {
            for (int attempt = 0; attempt < 2; ++attempt) {
                auto lib = ensure_logic(pref_dir(), base / "logic.bin", expected_logic_build_id());
                if (!lib.has_value()) { l = lib.error(); break; }
                l = load_logic(lib.value(), expected_logic_build_id());
                if (l.has_value()) break;
                std::filesystem::remove_all(lib.value().parent_path(), ec); // damaged: unlock it again
            }
        } else {
            l = load_logic(base / logic_library_name(), expected_logic_build_id());
        }
        if (l.has_value()) logic = std::move(l.value());
        else problem = "Mad TV cannot start: " + l.error().reason + " (" + l.error().file + ")";
    }
    if (logic) {
        try {
            game = std::make_unique<game::Game>(game::GameConfig{utf8_path(data_dir), pref_dir() / "save", settings.language, &logic->tables()});
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            problem = std::string("Mad TV cannot start: ") + e.what();
        }
    }
    if (!game && !options.smoke_test) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mad TV", problem.c_str(), window);
        renderer.reset(); SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
#endif
#ifdef MTV_HAVE_GAME
    // AdLib output: the translated driver's register writes, rendered at the chip's own rate. No device, no sound.
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, int(audio::Opl::kSampleRate)};
    // A smoke test runs slower than the game's clock, so its sound would stutter: it has none, except the real-time
    // smoke test, which plays muted so the audio gaps can be counted.
    const bool audio_on = game && (!options.smoke_test || options.realtime);
    SDL_AudioStream* sound = audio_on ? SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr) : nullptr;
    if (sound && options.smoke_test) SDL_SetAudioStreamGain(sound, 0.0f);
    if (sound) SDL_ResumeAudioStreamDevice(sound);
    else if (audio_on) std::cerr << "no audio: " << SDL_GetError() << '\n';
    audio::OplStream opl(8000);
    const audio::AudioPacer pacer(audio::Opl::kSampleRate, 60, 150);
    std::vector<int16_t> samples, silence;
    uint64_t sound_time = game ? game->machine().cpu.icount : 0;
    uint64_t audio_feeds = 0, audio_refills = 0; // refills after the first feed are gaps the listener hears
    std::vector<uint64_t> audio_refill_frames;
#endif
    // Upscaled pictures need the pack; without one the game is shown with nearest-neighbour scaling.
    std::unique_ptr<render::Pack> pack;
    const auto pack_dir = options.pack_dir.empty() ? pref_dir() / "pack" : utf8_path(options.pack_dir);
    if (auto p = render::Pack::open(pack_dir); p.has_value()) pack = std::move(p.value());
    else std::cerr << "no upscale pack at " << pack_dir.string() << " (" << p.error().reason << "), using nearest\n";
    render::Compositor compositor(pack.get());
    // Without a pack, the art is built here from the player's files: on the first start, and later from F12. The game
    // waits meanwhile; Esc skips it, and without a pack the game is drawn with plain 6x pixels.
    PackJob pack_job;
    bool pack_job_seen = false;
    std::string notice;
    uint64_t notice_until = 0;
    Upscaler upscaler = [&] {
        const char* b = SDL_GetBasePath();
        Upscaler u = upscaler_in(utf8_path(b ? b : ""));
        if (const char* r = std::getenv("MTV_RUNNER"); r && *r) u = {utf8_path(r), utf8_path(r).parent_path() / "models"};
        return u;
    }();
    auto start_pack_job = [&] {
        pack_job.start(utf8_path(data_dir), upscaler, pack_dir, pref_dir() / "pack-work");
        pack_job_seen = false;
    };
    if (!pack && !data_dir.empty() && !settings.pack_build_tried && (!options.smoke_test || options.build_pack)) {
        settings.pack_build_tried = true;
        if (!options.smoke_test)
            if (auto r = save_settings(settings, settings_path); !r.has_value()) std::cerr << "settings: " << r.error().reason << '\n';
        start_pack_job();
    }
    // INTRO.EXE (the fuse, fire and logo) runs first, as the original's start batch did; a key or click skips it.
    std::unique_ptr<IntroData> intro_data;
    std::unique_ptr<Intro> intro;
    if (!data_dir.empty() && (!options.smoke_test || options.intro)) {
        if (auto d = IntroData::load(utf8_path(data_dir)); d.has_value()) {
            intro_data = std::make_unique<IntroData>(std::move(d.value()));
            intro = std::make_unique<Intro>(*intro_data);
        } else std::cerr << "intro: " << d.error().reason << '\n';
    }
    bool skip_intro = false;
    ui::OptionsMenu menu;
    menu.set_pack_available(pack != nullptr);
    auto apply = [&](ui::OptionChange ch) {
        if (ch == ui::OptionChange::None || ch == ui::OptionChange::Closed) return;
        if (ch == ui::OptionChange::BuildPack) { if (!pack_job.running()) start_pack_job(); return; }
        if (ch == ui::OptionChange::Fullscreen) SDL_SetWindowFullscreen(window, settings.fullscreen);
#ifdef MTV_HAVE_GAME
        if (ch == ui::OptionChange::Language && game) {
            try { game->set_language(settings.language); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
        }
#endif
        if (!options.smoke_test)
            if (auto r = save_settings(settings, settings_path); !r.has_value()) std::cerr << "settings: " << r.error().reason << '\n';
    };
    InputState input;
    Ticker ticker;
    nlohmann::json script = nlohmann::json::array();
    if (!options.script_path.empty()) {
        std::ifstream f(utf8_path(options.script_path));
        script = nlohmann::json::parse(f, nullptr, false);
        if (!script.is_array()) { std::cerr << "script: not a JSON list\n"; script = nlohmann::json::array(); }
    }
    size_t next_event = 0;
    std::vector<uint8_t> blank(kLogicalWidth * kLogicalHeight * 3, 0);
    std::uint32_t frames = 0;
    bool quit = false, failed = false;
    while (!quit && (!options.smoke_test || frames < options.frames)) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_KEY_DOWN: {
                if (pack_job.running()) { if (e.key.key == SDLK_ESCAPE) pack_job.cancel(); break; }
                const PortAction action = port_shortcut(e.key.key, e.key.mod, settings.keys);
                if (!intro && (action == PortAction::Options || (menu.is_open() && e.key.key == SDLK_ESCAPE))) {
                    menu.set_open(!menu.is_open());
                } else if (menu.is_open() && action == PortAction::None) {
                    if (e.key.key == SDLK_UP) menu.move(-1);
                    else if (e.key.key == SDLK_DOWN) menu.move(1);
                    else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) apply(menu.activate_selected(settings));
                } else if (action == PortAction::Fullscreen) {
                    menu.set_open(false);
                    apply(menu.activate(ui::OptionItem::Fullscreen, settings));
                } else if (action == PortAction::Pictures) {
                    apply(menu.activate(ui::OptionItem::Pictures, settings));
                } else if (action != PortAction::None) {
                    // the options keys during the intro: nothing, and not a key for the intro either
                } else if (intro) {
                    skip_intro = true;
                } else {
                    input.key(e.key.key, e.key.mod);
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                int w = 0, h = 0, pw = 0, ph = 0;
                SDL_GetWindowSize(window, &w, &h);
                SDL_GetWindowSizeInPixels(window, &pw, &ph);
                auto p = Viewport::window_to_logical({e.motion.x, e.motion.y}, {w, h, pw, ph}, settings.aspect);
                if (menu.is_open()) {
                    SDL_ShowCursor();
                    if (p) if (auto i = menu.item_at(*p)) { menu.move(*i - menu.selected()); }
                } else if (p) { input.mouse_move(*p); SDL_HideCursor(); } else SDL_ShowCursor();
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
                if (pack_job.running()) break;
                if (menu.is_open()) {
                    int w = 0, h = 0, pw = 0, ph = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    SDL_GetWindowSizeInPixels(window, &pw, &ph);
                    auto p = Viewport::window_to_logical({e.button.x, e.button.y}, {w, h, pw, ph}, settings.aspect);
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && p)
                        if (auto i = menu.item_at(*p)) apply(menu.activate(menu.items()[size_t(*i)], settings));
                    break;
                }
                if (intro) { if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) skip_intro = true; break; }
                input.mouse_button(e.button.button, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            default: break;
            }
        }
        if (quit) break;
        if (pack_job.finished() && !pack_job_seen) {
            pack_job_seen = true;
            pack_job.join();
            const auto err = pack_job.error();
            if (!err) {
                if (auto p = render::Pack::open(pack_dir); p.has_value()) {
                    pack = std::move(p.value());
                    compositor.set_pack(pack.get());
                    notice = settings.language == game::Language::German ? "Hochskalierte Bilder sind fertig." : "Upscaled pictures are ready.";
                } else notice = "Upscaled pictures: " + p.error().reason;
            } else {
                std::cerr << "upscaled pictures not built: " << *err << '\n';
                notice = settings.language == game::Language::German ? "Ohne hochskalierte Bilder (F12 oder Strg+O: erneut versuchen)."
                                                                     : "No upscaled pictures (F12 or Ctrl+O to try again).";
            }
            std::cerr << notice << '\n';
            notice_until = SDL_GetTicks() + 6000;
            menu.set_pack_available(pack != nullptr);
        }
        if (pack_job.running()) {
            const bool de = settings.language == game::Language::German;
            if (!renderer->render_screen(blank).has_value()) { failed = true; break; }
            renderer->overlay_box(40, 70, 240, 60, 24, 28, 48, 235);
            renderer->overlay_text(de ? "Bilder werden hochskaliert" : "Upscaling the pictures", 52, 78, 36);
            const size_t total = pack_job.total();
            renderer->overlay_text(total ? std::to_string(pack_job.done() * 100 / total) + " %" : (de ? "Vorbereitung" : "Preparing"), 52, 96, 32);
            renderer->overlay_text(de ? "Esc: ohne spielen" : "Esc: play without them", 52, 112, 32);
            const bool shot = options.smoke_test && frames + 1 == options.frames && !options.screenshot_path.empty();
            if (!renderer->present(settings.aspect, shot ? utf8_path(options.screenshot_path) : std::filesystem::path{}).has_value()) { failed = true; break; }
            ++frames;
            SDL_Delay(options.smoke_test ? 50 : 16);
            continue;
        }
        const std::vector<uint8_t>* screen = &blank;
        bool upscaled_drawn = false;
        std::vector<uint8_t> intro_rgb;
        if (intro) {
            const int n = (options.smoke_test && !options.realtime) ? 1 : ticker.advance(int64_t(SDL_GetTicksNS()));
            bool running = true;
            for (int i = 0; i < n && running; ++i) { running = intro->step(skip_intro); skip_intro = false; }
            if (running) {
                const auto& f = intro->frame();
                intro_rgb.resize(64000 * 3);
                for (size_t i = 0; i < 64000 && i < f.image.indices.size(); ++i)
                    for (size_t k = 0; k < 3; ++k) intro_rgb[i * 3 + k] = f.rgb[f.image.indices[i] & 15][k];
                screen = &intro_rgb;
            } else {
                intro.reset();
            }
        }
        if (!intro_rgb.empty()) {
            const bool shot = options.smoke_test && frames + 1 == options.frames && !options.screenshot_path.empty();
            if (!renderer->render_screen(*screen).has_value() ||
                !renderer->present(settings.aspect, shot ? utf8_path(options.screenshot_path) : std::filesystem::path{}).has_value()) { failed = true; break; }
            ++frames;
            if (options.smoke_test && !options.realtime) SDL_Delay(1);
            continue;
        }
#ifdef MTV_HAVE_GAME
        std::vector<uint8_t> rgb;
        if (game) {
            while (next_event < script.size() && script[next_event].value("frame", 0) <= int(frames)) {
                const auto& ev = script[next_event++];
                if (ev.contains("mouse")) input.mouse_move({ev["mouse"][0].get<int>(), ev["mouse"][1].get<int>()});
                if (ev.contains("buttons")) { const int b = ev["buttons"]; input.mouse_button(SDL_BUTTON_LEFT, b & 1); input.mouse_button(SDL_BUTTON_RIGHT, b & 2); }
                if (ev.contains("keys")) for (const auto& k : ev["keys"]) input.raw_key(k.get<uint16_t>());
                if (ev.contains("host_keys"))  // window keys, through the app's own event handling
                    for (const auto& k : ev["host_keys"]) {
                        SDL_Event kd{};
                        kd.type = SDL_EVENT_KEY_DOWN;
                        kd.key.key = SDL_GetKeyFromName(k.get<std::string>().c_str());
                        kd.key.down = true;
                        SDL_PushEvent(&kd);
                    }
            }
            int n = (options.smoke_test && !options.realtime) ? 1 : ticker.advance(int64_t(SDL_GetTicksNS()));
            if (menu.is_open()) n = 0; // the game waits while the options are open
            for (int i = 0; i < n && !quit; ++i) {
                if (!game->tick(input.take_tick())) {
                    quit = true;
                    if (auto f = game->failure()) {
                        std::cerr << "Mad TV: " << *f << '\n';
                        if (!options.smoke_test) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mad TV", ("Mad TV stopped: " + *f).c_str(), window);
                        failed = true;
                    }
                }
                auto& m = game->machine();
                opl.render(sound_time, m.cpu.icount, m.opl_writes(), samples);
                m.clear_opl_writes();
                sound_time = m.cpu.icount;
            }
            if (sound && !samples.empty()) {
                const auto plan = pacer.plan(size_t(SDL_GetAudioStreamQueued(sound)) / 2, samples.size());
                if (plan.silence && audio_feeds) { ++audio_refills; if (audio_refill_frames.size() < 32) audio_refill_frames.push_back(frames); }
                ++audio_feeds;
                silence.assign(plan.silence, 0);
                if (!silence.empty()) SDL_PutAudioStreamData(sound, silence.data(), int(silence.size() * 2));
                if (plan.drop < samples.size()) SDL_PutAudioStreamData(sound, samples.data() + plan.drop, int((samples.size() - plan.drop) * 2));
            }
            samples.clear();
            if (pack && settings.graphics_dev == GraphicsMode::Upscaled && game->frame().mode == 0x13 && game->frame().tags.size() == 64000) {
                const auto& f = game->frame();
                const auto d = compositor.update(f.pixels, f.tags, f.dac, [&](render::Tag t) { return game->image_of(t); });
                if (!renderer->render_upscaled(compositor.canvas(), d.x, d.y, d.w, d.h).has_value()) { failed = true; break; }
                upscaled_drawn = true;
            } else {
                compositor.invalidate();
                rgb = game->frame_rgb();
                screen = &rgb;
            }
        }
#endif
        if (!upscaled_drawn && !renderer->render_screen(*screen).has_value()) { failed = true; break; }
        if (menu.is_open()) {
            const auto pnl = menu.panel();
            renderer->overlay_box(0, 0, 320, 200, 0, 0, 0, 120);
            renderer->overlay_box(pnl.x, pnl.y, pnl.w, pnl.h, 24, 28, 48, 235);
            renderer->overlay_text(menu.title(settings), pnl.x + 12, pnl.y + 8, 36);
            for (int i = 0; i < int(menu.items().size()); ++i) {
                const auto r = menu.item_rect(i);
                if (i == menu.selected()) renderer->overlay_box(r.x - 2, r.y - 1, r.w + 4, r.h + 2, 90, 60, 160, 255);
                renderer->overlay_text(menu.label(menu.items()[size_t(i)], settings), r.x, r.y + 2, 32);
            }
        }
        if (!notice.empty() && SDL_GetTicks() < notice_until) {
            renderer->overlay_box(0, 188, 320, 12, 0, 0, 0, 200);
            renderer->overlay_text(notice, 4, 190, 28);
        }
        const bool last = options.smoke_test && frames + 1 == options.frames;
        if (last && !options.screenshot_path.empty()) {
            if (auto r = renderer->present(settings.aspect, utf8_path(options.screenshot_path)); !r.has_value()) { std::cerr << r.error().reason << '\n'; failed = true; break; }
        } else if (!renderer->present(settings.aspect).has_value()) { failed = true; break; }
        ++frames;
        if (options.smoke_test && !options.realtime) SDL_Delay(50);
    }
#ifdef MTV_HAVE_GAME
    if (sound) SDL_DestroyAudioStream(sound);
#endif
    // An art build still running stops here, while SDL is up: the runner is killed and the worker joins.
    pack_job.cancel();
    pack_job.join();
    const std::string backend = renderer->backend();
    renderer.reset();
    SDL_DestroyWindow(window);
    if (!options.smoke_test) {
        if (auto r = save_settings(settings, settings_path); !r.has_value()) std::cerr << "settings: " << r.error().reason << '\n';
    }
    SDL_Quit();
    const bool complete = !failed && (!options.smoke_test || frames == options.frames);
    if (!options.report_path.empty()) {
        nlohmann::json report{{"schema_version", 1}, {"platform", SDL_GetPlatform()},
#if defined(__aarch64__) || defined(_M_ARM64)
            {"architecture", "arm64"},
#elif defined(__x86_64__) || defined(_M_X64)
            {"architecture", "x86_64"},
#else
            {"architecture", "unknown"},
#endif
            {"sdl_version", SDL_GetVersion()}, {"backend", backend}, {"frames_presented", frames}, {"exit_code", complete ? 0 : 1}};
#ifdef MTV_HAVE_GAME
        if (options.realtime) { report["audio_feeds"] = audio_feeds; report["audio_refills"] = audio_refills; report["audio_refill_frames"] = audio_refill_frames; }
#endif
        std::ofstream out(options.report_path);
        out << report.dump(2) << '\n';
        if (!out) { std::cerr << "report write failed\n"; return 1; }
    }
    return complete ? 0 : 1;
}
}
