// Game-side changes for the picture: filling the frame, and (phase 07) the
// widescreen view.
//
// Registered at cartridge addresses from on_init (bh::install_widescreen, called
// by register_runtime_functions). Every call in this port is resolved by address,
// so a function registered here replaces the game's for every caller.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "ultramodern/config.hpp"

#include "bh/callbacks.h"

extern "C" void setFullResolution(uint8_t* rdram, recomp_context* ctx);

namespace {

bool env_off(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v == '0';
}

// NAME=1 (anything but empty or 0) switches a change off.
bool env_off_flag(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
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

// ---- the cull angle, widened to the window ---------------------------------
//
// Symptom: at 21:9 terrain tiles are missing at both sides of the picture --
// sawtooth edges with sky showing through -- and anything near the edges pops.
//
// Cause: RT64 widens the projection; the game still culls against its own view
// cone. One value holds that cone for every test: D_8014FD2A, the full horizontal
// cull angle in binary angle units (0x10000 = 360 degrees), recomputed by
// func_800B33BC_C236C(pitch) from a 33-degree half-angle (D_80142E20) and the
// camera pitch. The terrain tile test (func_800B960C_C85BC) and the entity tests
// (func_800B93AC_C835C, func_800B9228_C81D8) all read it; 0x8000 disables them.
//
// Fix: run the game's function, then widen what it wrote: the half-angle's tangent
// is scaled by the window's aspect over 4:3 (Hor+, as RT64 widens the view),
// plus BH_CULL_MARGIN percent (default 10) for the interpolated frames between
// two game updates. An absolute value derived from the game's own each call, so
// nothing compounds (playbook 08, "feedback compounds"). At 4:3, or with aspect
// ratio Original, it only adds the margin. BH_NO_WIDE_CULL=1 leaves the game's.
//
// func_800B33BC is in the outside overlay, so the wrapper is registered again
// every time that overlay loads (bh::overlay_loaded).
extern "C" void func_800B33BC_C236C(uint8_t* rdram, recomp_context* ctx);
constexpr uint32_t kCullAngleFunc = 0x800B33BC;
constexpr gpr kCullAngle = static_cast<gpr>(static_cast<int32_t>(0x8014FD2A));

float cull_margin() {
    static const float margin = [] {
        const char* v = std::getenv("BH_CULL_MARGIN");
        const double pct = v != nullptr ? std::atof(v) : 10.0;
        return static_cast<float>(1.0 + std::clamp(pct, 0.0, 200.0) / 100.0);
    }();
    return margin;
}

void cull_angle_widened(uint8_t* rdram, recomp_context* ctx) {
    func_800B33BC_C236C(rdram, ctx);
    const uint16_t game = static_cast<uint16_t>(MEM_HU(0, kCullAngle));
    if (game >= 0x8000) {
        return;   // the game's own "no culling" (steep pitch)
    }
    float aspect = 4.0f / 3.0f;
    if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Original) {
        aspect = std::max(aspect, bh::window_aspect());
    }
    const double half = (game * 0.5) * (2.0 * 3.14159265358979 / 65536.0);
    const double wide_half = std::atan(std::tan(half) * (aspect / (4.0 / 3.0)) * cull_margin());
    const double wide = 2.0 * wide_half * (65536.0 / (2.0 * 3.14159265358979));
    const uint16_t value = wide >= 0x7FFF ? 0x8000 : static_cast<uint16_t>(wide);
    MEM_H(0, kCullAngle) = static_cast<int16_t>(value);

    // The first few distinct inputs; BH_CULL_TRACE=1 reports every change.
    static uint16_t last_game = 0;
    static int reports = 0;
    static const bool trace = std::getenv("BH_CULL_TRACE") != nullptr;
    if (game != last_game && (reports < 3 || trace)) {
        last_game = game;
        ++reports;
        std::fprintf(stderr, "[bh] cull angle 0x%04X -> 0x%04X (aspect %.3f, margin %.2f; BH_NO_WIDE_CULL=1 keeps the game's)\n",
                     game, value, aspect, cull_margin());
        std::fflush(stderr);
    }
}

}  // namespace

namespace bh {

void overlay_loaded(size_t overlay_id) {
    // recomp/overlays.txt order: 1 = .overlay_gameplay_outside.
    if (overlay_id == 1 && !env_off_flag("BH_NO_WIDE_CULL")) {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kCullAngleFunc), cull_angle_widened);
    }
}

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
