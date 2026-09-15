// Wires RecompFrontend's launcher, ROM picker and config menu in.
//
// See include/bh/frontend.h for what this is and why it is optional. Copied
// from Wave Race 64: Recompiled's src/frontend.cpp; what is left out is Wave
// Race's own (its water, music, draw-distance and field-of-view options, and the
// display-list rewriter), and what is added is from Pilotwings 64's and Rayman
// 2's copies of the same file (the bindings written on first run and on exit,
// and BH_AUTOSTART).
//
// The division of labour is worth stating, because almost none of it is ours.
// RecompFrontend owns the launcher, the native file dialog, the ROM validation
// error messages, the settings tabs, the controller remapping and the profiles
// that persist between sessions. What a port supplies is three things: which
// game this is, what the menu entries should say, and a stylesheet.

#include "bh/frontend.h"
#include "bh/callbacks.h"
#include "bh/dlcensus.h"
#include "bh/hudrewrite.h"
#include "bh/inspector.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <recompui/recompui.h>
#include <recompui/config.h>
#include <recompui/program_config.h>
#include <recompui/renderer.h>
#include <recompinput/players.h>
#include <recompinput/input_mapping.h>
#include <recompinput/profiles.h>

#include <librecomp/config.hpp>
#include <librecomp/game.hpp>
#include <ultramodern/config.hpp>

#include "bh/rom.h"

// The two globals recompui expects the port to define. It declares them extern
// in its own translation units and links against whatever the port provides:
//
//     extern std::vector<recomp::GameEntry> supported_games;   // ui_launcher.cpp
//     extern SDL_Window* window;                               // ui_state.cpp
//
// They are in the global namespace because that is where the library looks for
// them. `supported_games` is what the launcher falls back to when a port does
// not register its own init callback; it is populated anyway so the two
// descriptions of this game cannot drift apart.
std::vector<recomp::GameEntry> supported_games;
SDL_Window* window = nullptr;

namespace {

// Where recompui keeps its settings: the directory main.cpp registered with
// librecomp, which is what recompui's own load and save use. Looking beside the
// executable instead made settings saved in the menu come back as defaults
// whenever the game was started from another directory (Wave Race 64).
std::filesystem::path config_directory() {
    return recomp::get_config_path();
}

// The key bindings, in the file recompui itself loads them from in finalize().
std::filesystem::path controls_config_path() {
    return config_directory() / (recompui::config::controls::id + ".json");
}

ultramodern::renderer::PresentationMode presentation_mode();

// Sits between ultramodern and RecompFrontend's renderer so every display list
// can be looked at before RT64 sees it: the HUD inspector's feed and the census
// (BH_DL_CENSUS, include/bh/dlcensus.h), and the HUD rewriter's copy when an
// element has a class (include/bh/hudrewrite.h). Wave Race 64's RewritingContext
// and Hybrid Heaven's CensusContext have the same shape.
class CensusContext final : public ultramodern::renderer::RendererContext {
public:
    CensusContext(uint8_t* rdram, std::unique_ptr<ultramodern::renderer::RendererContext> inner)
        : rdram_(rdram), inner_(std::move(inner)) {
        setup_result = inner_->get_setup_result();
        chosen_api = inner_->get_chosen_api();
    }

    bool valid() override { return inner_->valid(); }
    ultramodern::renderer::SetupResult get_setup_result() const override {
        return inner_->get_setup_result();
    }
    ultramodern::renderer::GraphicsApi get_chosen_api() const override {
        return inner_->get_chosen_api();
    }
    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        return inner_->update_config(old_config, new_config);
    }
    void enable_instant_present() override { inner_->enable_instant_present(); }
    void send_dummy_workload(uint32_t fb_address) override { inner_->send_dummy_workload(fb_address); }
    void update_screen() override { inner_->update_screen(); }
    void shutdown() override { inner_->shutdown(); }
    uint32_t get_display_framerate() const override { return inner_->get_display_framerate(); }
    float get_resolution_scale() const override { return inner_->get_resolution_scale(); }

    void send_dl(const OSTask* task) override {
        bh::dlcensus::per_frame(rdram_, static_cast<uint32_t>(task->t.data_ptr));
        // HUD classes, live (include/bh/hudrewrite.h): submit a rewritten copy
        // while any element has a class other than centre.
        if (const uint32_t rewritten = bh::hudrewrite::rewrite(rdram_, static_cast<uint32_t>(task->t.data_ptr))) {
            OSTask copy = *task;
            copy.t.data_ptr = rewritten;
            if (bh::dlcensus::wanted()) {
                bh::dlcensus::run(rdram_, static_cast<uint32_t>(task->t.data_ptr));
            }
            inner_->send_dl(&copy);
            return;
        }
        if (bh::dlcensus::wanted()) {
            bh::dlcensus::run(rdram_, static_cast<uint32_t>(task->t.data_ptr));
        }
        inner_->send_dl(task);
    }

private:
    uint8_t* rdram_;
    std::unique_ptr<ultramodern::renderer::RendererContext> inner_;
};

// RecompFrontend's renderer draws the game and the menus into the same command
// list, so it replaces a renderer of the port's own rather than sitting beside
// it.
std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
        uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
        bool developer_mode) {
    // RT64 already binds F1 to its developer UI, and the port's HUD inspector
    // draws inside it. Every path to that UI is gated on RT64's developer mode,
    // though: the key handler, the event filter RT64 installs for itself, and
    // State::inspect() at the other end. So it is on unconditionally here.
    //
    // A debug menu that only exists in a build made for it is a debug menu
    // nobody has when they need it -- the person looking at a misplaced menu
    // element is running the game they downloaded. Nothing is drawn until F1 is
    // pressed: RT64 creates its inspector on the keystroke and State::inspect()
    // returns immediately while there is none, so the cost of leaving this on
    // is a null check per frame.
    (void)developer_mode;
    bh::inspector::install();
    return std::make_unique<CensusContext>(
        rdram, recompui::renderer::create_render_context(
                   rdram, window_handle, presentation_mode(), true));
}

// Which frame RT64 puts on screen, and when.
//
// Console shows what the N64's video interface would have shown: the buffer
// the game finished a frame or more ago. That is the faithful choice, and it
// also switches the Framerate setting off. RT64 only generates frames between
// two game frames when the buffer it just drew is the one being presented,
// which under Console never happens for a game that buffers at all -- Wave Race
// 64's port found its Display and Manual options changed nothing until this was
// changed.
//
// PresentEarly shows each frame as soon as it is drawn, which is what the other
// recompiled ports do. It takes latency off, and it is what lets RT64
// interpolate. SkipBuffering is the middle ground. BH_PRESENT_MODE picks one by
// name for comparing them; it is a testing knob, not a setting. Phase 08 measures
// which one this game needs.
ultramodern::renderer::PresentationMode presentation_mode() {
    using Mode = ultramodern::renderer::PresentationMode;
    const char* env = std::getenv("BH_PRESENT_MODE");
    if (env != nullptr) {
        const std::string_view value{ env };
        if (value == "console") return Mode::Console;
        if (value == "skip")    return Mode::SkipBuffering;
        if (value == "early")   return Mode::PresentEarly;
        std::fprintf(stderr, "[bh] BH_PRESENT_MODE=%s not recognised (console, skip, early); using early\n", env);
    }
    return Mode::PresentEarly;
}

// Builds the launcher's menu. Called once, when the launcher is created.
//
// add_start_game_or_load_rom_option is the whole first-run flow: with no valid
// dump it reads "Load ROM" and opens a file dialog, validates what comes back
// against the hash this project is pinned to, and reports which way it failed;
// once a dump is accepted it becomes "Start Game" and calls into librecomp.
// Nothing about that is written here, which is the point of the library.
void build_launcher(recompui::LauncherMenu* menu) {
    recompui::GameOptionsMenu* options = menu->init_game_options_menu(
        std::u8string{ bh::kGameId },
        bh::kModGameId,
        bh::kDisplayName,
        // No thumbnail: any artwork for the game itself would be taken from
        // the cartridge, and this project does not ship anything derived from
        // a dump. The launcher lays out fine without one.
        {});

    // The launcher's own background, distinct from the game thumbnail above:
    // original artwork supplied with the project (assets/icons/Logo.svg, two
    // low-polygon saucers drawn by tools/make_logo.py), not derived from the
    // cartridge. It has no lettering, so the library's plain-text title stays
    // (playbook 07: remove it only when the art spells the name).
    menu->set_launcher_background_svg("icons/Logo.svg");

    options->add_start_game_or_load_rom_option("Load ROM", "Start Game");
    options->add_setup_controls_option("Controls");
    options->add_settings_option("Settings");
    options->add_mods_option("Mods");
    // Closing the window works, but a menu the pad can reach should not need a
    // mouse to leave. add_exit_option calls ultramodern::quit(), which unwinds
    // the game thread and the renderer in order rather than tearing the process
    // down, so the EEPROM save is flushed on the way out.
    options->add_exit_option("Quit");
}

}  // namespace

namespace bh::frontend {

void init() {
    // The program's own identity, as distinct from the game's. recompui shows
    // the name in the launcher and uses the id to decide where settings and
    // controller profiles are stored, so both must be set before anything is
    // built -- the launcher throws from its constructor otherwise. The id is the
    // settings folder's name, which main.cpp registers with librecomp: the two
    // must be equal (playbook 07).
    recompui::programconfig::set_program_name("Body Harvest: Recompiled");
    recompui::programconfig::set_program_id(u8"body-harvest-recomp");

    // The family name is the one inside the font, not the filename: this file
    // is LatoLatin-Regular.ttf and declares itself "LatoLatin". Getting it wrong
    // is silent -- the UI lays out and draws with every element in place and no
    // text in any of them. The stylesheet has to name the same family.
    //
    // Lato is the face RmlUi vendors for its own samples, under the SIL Open
    // Font License; the build copies it next to the executable rather than
    // committing a second copy of a binary this repository already has.
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");

    // The keyboard layout the series' ports share (Wave Race 64, Beetle
    // Adventure Racing, Pilotwings 64), declared as the frontend's defaults so
    // that the keys in docs/BUILDING.md are the keys a fresh profile is bound to.
    // RecompFrontend's own defaults are a different scheme (WASD and space).
    //
    // Defaults apply to a profile the first time it is created; a keyboard
    // profile already saved keeps whatever it holds until it is reset in the
    // controls tab.
    {
        using recompinput::GameInput;
        using recompinput::InputField;
        const struct { GameInput input; SDL_Scancode key; } keys[] = {
            { GameInput::X_AXIS_NEG,  SDL_SCANCODE_LEFT },
            { GameInput::X_AXIS_POS,  SDL_SCANCODE_RIGHT },
            { GameInput::Y_AXIS_POS,  SDL_SCANCODE_UP },
            { GameInput::Y_AXIS_NEG,  SDL_SCANCODE_DOWN },
            { GameInput::A,           SDL_SCANCODE_X },
            { GameInput::B,           SDL_SCANCODE_C },
            { GameInput::Z,           SDL_SCANCODE_Z },
            { GameInput::START,       SDL_SCANCODE_RETURN },
            { GameInput::L,           SDL_SCANCODE_A },
            { GameInput::R,           SDL_SCANCODE_S },
            // The C buttons, under the right hand while the left moves.
            { GameInput::C_UP,        SDL_SCANCODE_I },
            { GameInput::C_DOWN,      SDL_SCANCODE_K },
            { GameInput::C_LEFT,      SDL_SCANCODE_J },
            { GameInput::C_RIGHT,     SDL_SCANCODE_L },
            { GameInput::DPAD_UP,     SDL_SCANCODE_T },
            { GameInput::DPAD_DOWN,   SDL_SCANCODE_G },
            { GameInput::DPAD_LEFT,   SDL_SCANCODE_F },
            { GameInput::DPAD_RIGHT,  SDL_SCANCODE_H },
        };
        for (const auto& binding : keys) {
            recompinput::set_default_mapping_for_keyboard(
                binding.input, { InputField::keyboard(binding.key) });
        }
    }

    recompui::register_launcher_init_callback(build_launcher);

    // Body Harvest is one player (docs/PLAN.md D8). Single-player mode merges the
    // keyboard profile with every connected pad, so there is nothing to assign
    // (Rayman 2 and Pilotwings 64 do the same).
    recompinput::players::set_player_count_range(1, 1);
    recompinput::players::set_single_player_mode(true);

    // The prefab tabs. Body Harvest has no gyro or mouse control.
    //
    // Rumble strength is on, and it is the only control the feedback has: the
    // slider is 0-100, recompinput scales the motor by it, and zero is off. The
    // game drives a Rumble Pak through osMotorStart/Stop, which the runtime turns
    // into set_rumble calls (src/callbacks.cpp). Without the option the whole
    // rumble path in recompinput is skipped, so this line also turns rumble on.
    recompui::config::GeneralTabOptions general{};
    general.has_rumble_strength = true;
    general.has_gyro_sensitivity = false;
    general.has_mouse_sensitivity = false;

    recompui::config::create_general_tab(general);

    // The ordering rule every tab below respects: create_config_tab appends to a
    // vector of tabs and returns a reference into it, so creating a tab can
    // leave every reference an earlier create_*_tab returned dangling. Configure
    // each tab completely before creating the next (Wave Race 64, playbook 07).
    //
    // The Graphics tab is RecompFrontend's: resolution, aspect ratio, HUD
    // placement, frame rate, anti-aliasing and window mode are RT64 options the
    // library already exposes. The port's own graphics options join it as the
    // enhancements that need them are built (phases 07 and 08).
    recompui::config::create_graphics_tab();

    // Main Volume did nothing in Wave Race 64's port until it was wired: recompui
    // defines the slider and reads it back, and nothing upstream ever applies it.
    // The Sound tab has no Apply button, so the callback hears Load when the saved
    // setting is read at startup and Permanent on every step of the slider, which
    // is what makes the volume follow the handle as it moves.
    auto& sound = recompui::config::create_sound_tab();
    sound.add_option_change_callback(
        recompui::config::sound::options::main_volume,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&value)) {
                bh::set_audio_volume(*percent);
            }
        });

    sound.add_bool_option(
        "mute_unfocused", "Mute When Not In Focus",
        "Silences the game, and stops controller rumble, while another window has focus.",
        true);
    sound.add_option_change_callback(
        "mute_unfocused",
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const bool* mute = std::get_if<bool>(&value)) {
                bh::set_mute_when_unfocused(*mute);
            }
        });
    recompui::config::create_controls_tab();

    // Mods. The runtime half of this runs whether or not there is a tab:
    // recomp::start calls initialize_mods() and scan_mods() on its own, main.cpp
    // gives librecomp this game's mod id, and texture-pack mods are recognised by
    // src/mods.cpp. What the tab adds is a way to see what is installed, switch it
    // on and off, and read each mod's own options; the launcher entry opens it
    // without starting the game first.
    recompui::config::create_mods_tab();

    // No add_game_input calls: recompinput already knows the N64 controller,
    // and this game has no inputs beyond it.

    // Whether this is a first run, asked before finalize(): loading a missing
    // settings file writes one with the defaults, so afterwards it always exists.
    const bool first_run = !std::filesystem::exists(config_directory() / "graphics.json");

    // Loads the player's saved settings from disk. Must come after every tab.
    recompui::config::finalize();

    // The frontend saves bindings only from the Controls tab's close handler, so
    // a rebind followed by closing the whole menu with Escape was lost, and a
    // fresh install had no controls.json at all (Rayman 2, Pilotwings 64). The
    // defaults are written now if there is none, and shutdown() writes whatever
    // is in effect on the way out.
    if (!std::filesystem::exists(controls_config_path())) {
        recompinput::profiles::save_controls_config(controls_config_path());
    }

    // Play fullscreen at the display's own resolution and aspect ratio on a
    // first run. Resolution Auto and aspect Expand are already the library's
    // defaults; only the window mode is not.
    //
    // Only when the player had no saved graphics settings, so choosing Windowed
    // in the menu is respected from then on. It is written after finalize() --
    // the option map does not exist until the file has loaded. The Graphics tab
    // confirms its changes, so set_option_value only stages the value;
    // save_config() applies and writes it. ultramodern's copy, which the renderer
    // reads, is set as well, so the first window opens fullscreen either way.
    //
    // Not while a script drives the run: every clean-profile test would take over
    // the display (Beetle Adventure Racing, playbook 07).
    const bool scripted = std::getenv("BH_INPUT_SCRIPT") != nullptr;
    if (first_run && !scripted) {
        auto& graphics_config = recompui::config::get_graphics_config();
        graphics_config.set_option_value(
            recompui::config::graphics::options::wm_option,
            static_cast<uint32_t>(ultramodern::renderer::WindowMode::Fullscreen));
        graphics_config.save_config();

        ultramodern::renderer::GraphicsConfig gfx = ultramodern::renderer::get_graphics_config();
        gfx.wm_option = ultramodern::renderer::WindowMode::Fullscreen;
        ultramodern::renderer::set_graphics_config(gfx);

        std::fprintf(stderr, "[bh] no saved graphics settings; defaulting to fullscreen at the display\'s size\n");
    }

    // Test hook: BH_TEST_INSPECTOR=25 opens RT64's F1 menu -- the HUD inspector
    // included -- 25 seconds after startup, by posting the F1 key RT64's event
    // filter listens for, so a capture can show it.
    if (const char* spec = std::getenv("BH_TEST_INSPECTOR")) {
        const double delay = std::atof(spec) > 0.0 ? std::atof(spec) : 20.0;
        std::thread([delay]() {
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
            SDL_Event event{};
            event.type = SDL_KEYDOWN;
            event.key.state = SDL_PRESSED;
            event.key.keysym.scancode = SDL_SCANCODE_F1;
            event.key.keysym.sym = SDLK_F1;
            SDL_PushEvent(&event);
            std::fprintf(stderr, "[bh] test: pressed F1\n");
            std::fflush(stderr);
        }).detach();
    }

    // Test hook: BH_TEST_OPEN_SETTINGS=graphics@25 opens the settings menu on that
    // tab 25 seconds after startup, so a capture can show a tab as a player sees
    // it without anyone pressing Escape. Opening it changes nothing that is saved.
    if (const char* spec = std::getenv("BH_TEST_OPEN_SETTINGS")) {
        const std::string text = spec;
        const size_t at = text.find('@');
        const std::string tab = text.substr(0, at);
        const double delay = at == std::string::npos ? 20.0 : std::atof(text.c_str() + at + 1);
        std::thread([tab, delay]() {
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
            recompui::ContextId context = recompui::config::get_config_context_id();
            context.open();
            recompui::config::set_tab(tab);
            context.close();
            recompui::config::open();
            std::fprintf(stderr, "[bh] test: opened the settings menu on the %s tab\n", tab.c_str());
            std::fflush(stderr);
        }).detach();
    }

    std::fprintf(stderr, "[bh] frontend ready: launcher, ROM picker and config menu\n");
    std::fflush(stderr);
}

void publish_game(const recomp::GameEntry& game) {
    supported_games.clear();
    supported_games.push_back(game);
}

void publish_window(SDL_Window* sdl_window) {
    ::window = sdl_window;
}

ultramodern::renderer::callbacks_t renderer_callbacks() {
    ultramodern::renderer::callbacks_t callbacks{};
    callbacks.create_render_context = create_render_context;
    return callbacks;
}

bool capturing_input() {
    return recompui::is_context_capturing_input();
}

// From Rayman 2: Recompiled (RAYMAN2_AUTOSTART). It runs from the event pump,
// the thread that owns the window and the UI: start_game is safe anywhere,
// hide_all_contexts is not, and the launcher itself calls both from there. A
// second of frames first, so the launcher exists to be hidden.
void maybe_autostart() {
    static const bool armed = [] {
        const char* v = std::getenv("BH_AUTOSTART");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    if (!armed) {
        return;
    }
    static int frames = 0;
    static bool fired = false;
    if (fired || ++frames < 60) {
        return;
    }
    fired = true;

    const std::u8string game_id{ bh::kGameId };
    if (!recomp::is_rom_valid(game_id)) {
        std::fprintf(stderr, "[bh] BH_AUTOSTART: no dump has been accepted yet; choose one in the"
                             " launcher once, then this works\n");
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr, "[bh] BH_AUTOSTART: starting the game without the launcher\n");
    std::fflush(stderr);
    recomp::start_game(game_id, {});
    recompui::hide_all_contexts();
}

void shutdown() {
    recompinput::profiles::save_controls_config(controls_config_path());
}

}  // namespace bh::frontend
