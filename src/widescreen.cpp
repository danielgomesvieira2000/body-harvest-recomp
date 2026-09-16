// Game-side changes for the picture: filling the frame, and (phase 07) the
// widescreen view.
//
// Registered at cartridge addresses from on_init (bh::install_widescreen, called
// by register_runtime_functions). Every call in this port is resolved by address,
// so a function registered here replaces the game's for every caller.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "ultramodern/config.hpp"

#include "bh/callbacks.h"
#include "bh/hudrewrite.h"
#include "bh/inspector.h"

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

// ---- the interior cull, widened and lengthened ---------------------------------
//
// Symptom: inside a building, walls, floor cells and furniture vanish at the sides
// of a wide picture and at a distance (Daniel, the first Greece interior: "any item
// that is too far away goes invisible", the left wall gone).
//
// Cause: the inside overlay has its own test, untouched by D_8014FD2A.
// func_8007C428_1644E8(x, y, z, radius, <4 unused stack args>, cone) decides every
// floor/wall cell (func_8007453C_15C5FC, radius 0x48) and every room object
// (func_80074FF0_15D0B0, func_8007568C_15D74C, radius 1.5 x half the footprint).
// It transforms the point by the view matrix D_800E7350, takes its distance r and
// view-plane angle a (func_80003824), turns a by cone/2 and rejects when
//   sins(a) * r < -radius              (outside one edge of the cone)
//   sins(a - cone) * r > radius        (outside the other)
//   coss(a - cone) * r < 40 - radius   (behind the camera)
//   coss(a - cone) * r >= 961          (beyond a fixed distance)
// Every caller passes cone 0x238E (50 degrees) -- narrower than a 4:3 view with
// fovy 45 (58 degrees), with the radius as the slack -- and 961 units is ten cells.
//
// Fix: the same test, natively (the game's sins/coss/atan tables via the
// recompiled functions), with the cone's half-angle tangent scaled by the window
// aspect over 4:3 times BH_CULL_MARGIN (as outdoors), and the distance limit raised
// to the room's diagonal plus a cell (D_800E6460/64 cells of 96 units), never below
// the game's 961. Nothing in the game bounds its per-frame matrix pool (D_8005BB20 +
// 0x1E280 .. + 0x22B00, 283 matrices) or display list (+0x280 .. +0xE380), so a
// point only the widened test admits is still rejected when either has less than
// kReserve left: the player, NPCs and effects draw after the room.
// BH_NO_WIDE_INSIDE_CULL=1 leaves the game's test; BH_INSIDE_CULL_TRACE=1 prints,
// once a second, how many points each test admitted and how many the budget refused.
extern "C" void func_8007C428_1644E8(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80003824_4424(uint8_t* rdram, recomp_context* ctx);
extern "C" void sins(uint8_t* rdram, recomp_context* ctx);
extern "C" void coss(uint8_t* rdram, recomp_context* ctx);
constexpr uint32_t kInsideCullFunc = 0x8007C428;
constexpr gpr kViewMatrix = static_cast<gpr>(static_cast<int32_t>(0x800E7350));     // D_800E7350, f32[4][4]
constexpr gpr kRoomCellsX = static_cast<gpr>(static_cast<int32_t>(0x800E6460));     // D_800E6460
constexpr gpr kRoomCellsZ = static_cast<gpr>(static_cast<int32_t>(0x800E6464));     // D_800E6464
constexpr gpr kFrameBufferBase = static_cast<gpr>(static_cast<int32_t>(0x8005BB20));  // D_8005BB20
constexpr gpr kMatrixCursor = static_cast<gpr>(static_cast<int32_t>(0x8005BB38));   // D_8005BB38
constexpr gpr kInsideGfxCursor = static_cast<gpr>(static_cast<int32_t>(0x8005BB2C));  // D_8005BB2C

int32_t game_trig(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*), int32_t angle) {
    ctx->r4 = angle & 0xFFFF;
    fn(rdram, ctx);
    return static_cast<int32_t>(ctx->r2);
}

int32_t scaled(int32_t table, int32_t r) {
    return static_cast<int32_t>((static_cast<double>(static_cast<float>(table)) / 32768.0) * static_cast<double>(r));
}

struct InsideCullStats {
    uint32_t calls = 0, game_visible = 0, wide_visible = 0, budget_refused = 0, mismatches = 0;
};

void inside_cull_widened(uint8_t* rdram, recomp_context* ctx) {
    const int16_t x = static_cast<int16_t>(ctx->r4);
    const int16_t y = static_cast<int16_t>(ctx->r5);
    const int16_t z = static_cast<int16_t>(ctx->r6);
    const int32_t radius = static_cast<uint16_t>(ctx->r7);
    const int32_t cone = static_cast<int16_t>(MEM_H(0x22, ctx->r29));   // 9th argument, caller's sp + 0x20
    const gpr args[4] = {ctx->r4, ctx->r5, ctx->r6, ctx->r7};

    auto m = [&](int off) {
        float f;
        const uint32_t bits = static_cast<uint32_t>(MEM_W(off, kViewMatrix));
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    };
    const float fx = x, fy = y, fz = z;
    const int32_t x_view = static_cast<int32_t>(m(0x30) + ((fx * m(0x00) + fy * m(0x10)) + fz * m(0x20)));
    const int32_t depth = -static_cast<int32_t>(m(0x38) + ((fx * m(0x08) + fy * m(0x18)) + fz * m(0x28)));
    const uint32_t sq = static_cast<uint32_t>(x_view) * static_cast<uint32_t>(x_view) +
                        static_cast<uint32_t>(depth) * static_cast<uint32_t>(depth);
    const int32_t r = static_cast<int32_t>(std::sqrt(static_cast<float>(static_cast<int32_t>(sq))));

    ctx->f12.fl = static_cast<float>(depth);
    ctx->f14.fl = static_cast<float>(x_view);
    func_80003824_4424(rdram, ctx);
    const int32_t bearing = static_cast<int32_t>(ctx->r2);

    auto test = [&](int32_t cone_bam, int32_t far_limit) {
        const int32_t a = cone_bam / 2 + bearing;
        if (scaled(game_trig(rdram, ctx, sins, a), r) < -radius) return false;
        const int32_t edge = a - cone_bam;
        const int32_t along = scaled(game_trig(rdram, ctx, coss, edge), r);
        if (radius < scaled(game_trig(rdram, ctx, sins, edge), r)) return false;
        if (along < 0x28 - radius) return false;
        return along < far_limit;
    };

    static InsideCullStats stats;
    static const bool trace = std::getenv("BH_INSIDE_CULL_TRACE") != nullptr;
    ++stats.calls;

    bool visible = test(cone, 0x3C1);
    if (trace) {
        // The native copy of the game's test must agree with the recompiled one.
        ctx->r4 = args[0]; ctx->r5 = args[1]; ctx->r6 = args[2]; ctx->r7 = args[3];
        func_8007C428_1644E8(rdram, ctx);
        if ((ctx->r2 != 0) != visible) ++stats.mismatches;
    }
    if (visible) {
        ++stats.game_visible;
    }
    else {
        float aspect = 4.0f / 3.0f;
        if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Original) {
            aspect = std::max(aspect, bh::window_aspect());
        }
        const double half = (cone * 0.5) * (2.0 * 3.14159265358979 / 65536.0);
        const double wide_half = std::atan(std::tan(half) * (aspect / (4.0 / 3.0)) * cull_margin());
        const int32_t wide_cone = std::min(static_cast<int32_t>(2.0 * wide_half * (65536.0 / (2.0 * 3.14159265358979))), 0x7FFF);
        const double cells_x = MEM_W(0, kRoomCellsX), cells_z = MEM_W(0, kRoomCellsZ);
        const int32_t far_limit = std::max<int32_t>(0x3C1, static_cast<int32_t>(std::hypot(cells_x, cells_z) * 96.0) + 96);
        if (test(wide_cone, far_limit)) {
            ++stats.wide_visible;
            // The frame's buffers, laid out by func_8000F368_FF68.
            constexpr uint32_t kReserveMatrices = 48 * 0x40;
            constexpr uint32_t kReserveGfx = 1024 * 8;
            const uint32_t base = static_cast<uint32_t>(MEM_W(0, kFrameBufferBase));
            const uint32_t mtx = static_cast<uint32_t>(MEM_W(0, kMatrixCursor));
            const uint32_t gfx = static_cast<uint32_t>(MEM_W(0, kInsideGfxCursor));
            const bool room = mtx + kReserveMatrices <= base + 0x22B00 && gfx + kReserveGfx <= base + 0xE380;
            visible = room;
            if (!room) ++stats.budget_refused;
        }
    }

    if (trace) {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::seconds(1)) {
            last = now;
            std::fprintf(stderr, "[bh] inside cull: %u tests, game admits %u, widened adds %u, budget refused %u, native/game mismatches %u\n",
                         stats.calls, stats.game_visible, stats.wide_visible, stats.budget_refused, stats.mismatches);
            std::fflush(stderr);
            stats = {};
        }
    }
    ctx->r2 = visible ? 1 : 0;
}

// ---- HUD widgets anchored to the edges ------------------------------------------
//
// Symptom: at 16:9 and wider the radar, the health and alien bars and the weapon
// panel stay in the middle 4:3 of the picture. The F1 panel could not move them:
// the radar, the bar fills and the bar icons are 2D triangles drawn straight into
// the main list, which the rewriter classified only as rectangles or called
// lists; and the bar frames share their textures between the left-hand bars and
// the vehicle's bars on the right, so a class per texture would drag one or the
// other to the wrong edge (docs/findings/phase-07.md, "HUD anchoring").
//
// The game says which side it means. func_8009C6CC_AB67C(x, y, fraction, side,
// icon, ...) draws every bar with its icon: the health and alien bars with side 0
// at x 0x50, the vehicle's with side 1 at width - 0x20 (decomp AAA70.c
// func_8009D96C). func_800A03FC_AF3AC is DisplayScanner, the radar;
// func_8013A764_149714 draws the weapon icon and ammo count. Each is wrapped:
// an anchor marker (include/bh/hudrewrite.h) goes into the game's display list
// before the call and an end marker after it, and the rewriter anchors
// everything in between. A class set in the panel or hud.json still wins.
// BH_NO_HUD_ANCHORS=1: off.
//
// All three live in the outside overlay, so they are registered again every time
// it loads, like the cull angle.
extern "C" void func_8009C6CC_AB67C(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_800A03FC_AF3AC(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8013A764_149714(uint8_t* rdram, recomp_context* ctx);
constexpr uint32_t kHudBarFunc = 0x8009C6CC;
constexpr uint32_t kScannerFunc = 0x800A03FC;
constexpr uint32_t kAmmoFunc = 0x8013A764;
constexpr gpr kDisplayListCursor = static_cast<gpr>(static_cast<int32_t>(0x8005BB2C));   // D_8005BB2C

void emit_anchor_marker(uint8_t* rdram, int cls) {
    const uint32_t cursor = static_cast<uint32_t>(MEM_W(0, kDisplayListCursor));
    if ((cursor >> 24) != 0x80) return;
    const gpr at = static_cast<gpr>(static_cast<int32_t>(cursor));
    MEM_W(0, at) = static_cast<int32_t>(bh::hudrewrite::kNoopOp);
    MEM_W(4, at) = static_cast<int32_t>(bh::hudrewrite::kAnchorMagic | static_cast<uint32_t>(cls));
    MEM_W(0, kDisplayListCursor) = static_cast<int32_t>(cursor + 8);
}

void hud_bar_anchored(uint8_t* rdram, recomp_context* ctx) {
    // a3: 0 = a left-hand bar, 1 = a right-hand one.
    const int cls = (ctx->r7 & 0xFF) != 0 ? bh::inspector::kRight : bh::inspector::kLeft;
    emit_anchor_marker(rdram, cls);
    func_8009C6CC_AB67C(rdram, ctx);
    emit_anchor_marker(rdram, bh::inspector::kAuto);
}

void scanner_anchored(uint8_t* rdram, recomp_context* ctx) {
    emit_anchor_marker(rdram, bh::inspector::kRight);
    func_800A03FC_AF3AC(rdram, ctx);
    emit_anchor_marker(rdram, bh::inspector::kAuto);
}

void ammo_anchored(uint8_t* rdram, recomp_context* ctx) {
    emit_anchor_marker(rdram, bh::inspector::kLeft);
    func_8013A764_149714(rdram, ctx);
    emit_anchor_marker(rdram, bh::inspector::kAuto);
}

}  // namespace

namespace bh {

void overlay_loaded(size_t overlay_id) {
    // recomp/overlays.txt order: 1 = .overlay_gameplay_outside.
    if (overlay_id == 1 && !env_off_flag("BH_NO_WIDE_CULL")) {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kCullAngleFunc), cull_angle_widened);
    }
    // 2 = .overlay_gameplay_inside.
    if (overlay_id == 2 && !env_off_flag("BH_NO_WIDE_INSIDE_CULL")) {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kInsideCullFunc), inside_cull_widened);
    }
    if (overlay_id == 1 && !env_off_flag("BH_NO_HUD_ANCHORS")) {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kHudBarFunc), hud_bar_anchored);
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kScannerFunc), scanner_anchored);
        recomp::overlays::add_loaded_function(static_cast<int32_t>(kAmmoFunc), ammo_anchored);
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
