#pragma once

// The launcher, ROM picker and config menu, from RecompFrontend.
//
// Everything here is what every N64: Recompiled port needs and none of it is
// specific to Body Harvest: a first-run flow that asks the player for their own
// dump and validates it, a settings menu, and rebindable input with per-device
// controller profiles. RecompFrontend implements all of it; this file is the
// small amount of wiring that tells it which game it is looking at. Copied from
// Wave Race 64: Recompiled (docs/PLAN.md, phase 06).
//
// It is compiled only when BH_WITH_FRONTEND is on. Without it the port keeps
// the phase 03 behaviour of taking a ROM path on the command line.

#include <SDL.h>

#include <librecomp/game.hpp>
#include <ultramodern/renderer_context.hpp>

namespace bh::frontend {

// Registers the fonts, the launcher menu and the config tabs. Must run before
// recomp::start(), which is what eventually shows them.
void init();

// recompui reaches into the port for two globals rather than being told them:
// a `supported_games` list and the SDL `window`. These publish ours into those,
// so there is one window and one game entry rather than two that can disagree.
void publish_game(const recomp::GameEntry& game);
void publish_window(SDL_Window* window);

// The renderer that draws the game *and* the UI. RecompFrontend's RT64 context
// replaces the project's own: the menus are drawn into the same command list as
// the game, so there is one renderer, not two.
ultramodern::renderer::callbacks_t renderer_callbacks();

// True while a menu is open and taking input. The game's controller reads are
// suppressed for as long as this holds.
//
// SDL events themselves are not handled here: recompinput::handle_events(),
// called directly from src/callbacks.cpp's poll_input(), is the only thing
// that drains SDL's event queue when the frontend is on.
bool capturing_input();

// BH_AUTOSTART=1: start the stored dump without waiting for the launcher's
// Start Game, so the shipped configuration can be scripted. Called every frame
// from the thread that owns the window; does nothing after the first start.
void maybe_autostart();

// Saves the key bindings in effect, so a rebind survives the menu being closed
// some other way than through the Controls tab. Call after recomp::start returns.
void shutdown();

}  // namespace bh::frontend
