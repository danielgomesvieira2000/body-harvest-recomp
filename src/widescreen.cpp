// Game-side changes for the picture: filling the frame, and (phase 07) the
// widescreen view.
//
// Registered at cartridge addresses from on_init (bh::install_widescreen, called
// by register_runtime_functions). Every call in this port is resolved by address,
// so a function registered here replaces the game's for every caller.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "librecomp/overlays.hpp"

extern "C" void setFullResolution(uint8_t* rdram, recomp_context* ctx);

namespace {

bool env_off(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v == '0';
}

// ---- gameplay at 320x240 ----------------------------------------------------
//
// Symptom: in outdoor gameplay the picture stops short of the frame -- black
// strips at the right (16 px of 320) and bottom (10 px of 240), in a 4:3 window
// as much as a wide one.
//
// Cause: entering gameplay the game calls setGameplayResolution()
// (0x80006D84: setVideoInterfaceXSize(0x130), setVideoInterfaceYSize(0xE6)), so it
// renders into a 304x230 region -- viewport, scissor, clears and several HUD
// positions are all computed from those two numbers (decomp core/FD80.c
// func_8000F368, core/53F0.c) -- and sets the VI's X/Y scale to 304/320 and
// 230/240 so the hardware stretches that region over the screen. Menus call
// setFullResolution() (320x240, scale 1.0). ultramodern ignores osViSetXScale /
// osViSetYScale and RT64 never presents with the VI scale, so the unscaled
// 304x230 region is what shows (BH_DL_CENSUS=300: viewport scale 152,115 trans
// 152,115; scissor 0,0..304,230).
//
// Fix: gameplay uses the full-resolution path too. The game then draws the whole
// 320x240 itself and asks for no VI scaling, which is also what lets RT64 treat
// the 3D pass as covering the frame when it widens it. What differs from the
// hardware picture: elements the game places at fixed coordinates (rather than
// from the size) sit 5% further from the right and 4% further from the bottom
// edge than the VI's stretch would have put them.
//
// BH_FULL_FRAME=0 restores the game's 304x230 for an A/B.
constexpr uint32_t kSetGameplayResolution = 0x80006D84;

void set_gameplay_resolution_full(uint8_t* rdram, recomp_context* ctx) {
    setFullResolution(rdram, ctx);
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "[bh] gameplay drawn at 320x240 (the game asked for 304x230; BH_FULL_FRAME=0 keeps it)\n");
        std::fflush(stderr);
    }
}

}  // namespace

namespace bh {

void install_widescreen() {
    if (env_off("BH_FULL_FRAME")) {
        std::fprintf(stderr, "[bh] BH_FULL_FRAME=0: gameplay keeps the game's 304x230 region\n");
    }
    else {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kSetGameplayResolution),
                                              set_gameplay_resolution_full);
    }
}

}  // namespace bh
