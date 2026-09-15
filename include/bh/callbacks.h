#pragma once

#include <cstddef>

#include <ultramodern/ultramodern.hpp>
#include <ultramodern/error_handling.hpp>
#include <ultramodern/events.hpp>
#include <ultramodern/input.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/threads.hpp>
#include <librecomp/rsp.hpp>

namespace bh {

// The platform I/O ultramodern deliberately does not own. Implemented on SDL2
// in src/callbacks.cpp.
ultramodern::input::callbacks_t          input_callbacks();
ultramodern::audio_callbacks_t           audio_callbacks();
recomp::rsp::callbacks_t                 rsp_callbacks();
ultramodern::gfx_callbacks_t             gfx_callbacks();
ultramodern::events::callbacks_t         events_callbacks();
ultramodern::error_handling::callbacks_t error_handling_callbacks();
ultramodern::threads::callbacks_t        threads_callbacks();
ultramodern::renderer::callbacks_t       renderer_callbacks();

// The Sound tab's Main Volume, 0-100, applied to every buffer on its way to the
// sound card. Nothing upstream consumes the setting: recompui defines the
// slider and leaves applying it to the port.
void set_audio_volume(double percent);

// The Sound tab's "Mute when the window is not in focus". On by default.
void set_mute_when_unfocused(bool mute);

void shutdown_platform();

// The game window's width over its height, or 4:3 before there is a window.
float window_aspect();

// Defined in src/overlays.cpp, which owns the generated section tables: hands
// them to librecomp. Call before start.
void register_overlays();
// Registers runtime-provided libultra at its cartridge addresses, every resident
// function, and the wrapper on the game's loader. Call from the game's on_init
// hook: init_overlays() clears the function map before it.
void register_runtime_functions();
size_t code_section_count();

// src/libultra_glue.cpp: state the runtime's libultra does not create but the
// game's own libultra code needs. Called by register_runtime_functions.
void install_libultra_glue();

// src/widescreen.cpp: game-side picture changes (full frame, widescreen view).
// Called by register_runtime_functions.
void install_widescreen();
// src/framestats.cpp: game frame rate vs presented rate (BH_FRAME_STATS) and
// interpolation pairing (BH_PAIRING). Called by register_runtime_functions.
void install_frame_stats();
// Re-registers game-side wrappers that live in an overlay, after it loads
// (recomp/overlays.txt order). Called by src/overlays.cpp.
void overlay_loaded(size_t overlay_id);

// src/calltrace.cpp: BH_TRACE_FUNCS=addr,... wraps those game addresses and prints
// each call's arguments and result. Call from on_init after every registration.
void install_call_traces();

}  // namespace bh
