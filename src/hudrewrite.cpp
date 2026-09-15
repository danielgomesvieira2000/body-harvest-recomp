// The HUD rewriter. See include/bh/hudrewrite.h.

#include "bh/hudrewrite.h"
#include "bh/gbi_f3dex.h"
#include "bh/hudid.h"
#include "bh/inspector.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// F3D family: RT64's hook command is G_SPNOOP (0x00), so F3DEX_GBI_2 stays undefined.
#include "rt64_extended_gbi.h"

namespace bh::hudrewrite {
namespace {

using namespace bh::gbi;

// Two scratch buffers, alternated per frame, in the upper 4 MB the game does not
// use (docs/findings/phase-00.md: no osMemSize reference; *inferred* free, as for
// Wave Race 64, whose rewriter used 0x80700000). Below the controller-latency
// timer at 0x807EF000 and the audio command-list copy at 0x807F0000.
constexpr uint32_t kScratch[2] = { 0x00700000u, 0x00740000u };
constexpr uint32_t kScratchSize = 0x40000u;

// ---- the player's model ------------------------------------------------------
//
// Adam is one call to a static model list (0x010031E0; Black Adam 0x050408F0)
// made after three matrices of the caller's: a base scale, his position and
// heading, and the root pose (decomp F9230.c func_800EF14C). Inside, the torso is
// drawn under those, then every other part pushes and multiplies a bone matrix
// from segment 7 -- `G_MTX 0x04 0x07000000 + 0x40 n`, fifteen of them built per
// frame by func_8000CC3C -- draws, and pops back up the chain
// (docs/findings/phase-08.md, "The player model").
//
// RT64 pairs each part with a part of the previous frame by the draw call's
// signature and the nearest position. Similar parts tie, a part takes another's
// previous pose or finds none, and the part is drawn a frame's motion away from
// the rest before snapping back: the model jitters while the world glides
// (Daniel; playbook 09, the Wave Race riders). So, as there, each part is paired
// by identity: an explicit group id per bone -- the bone matrix's segmented
// address, which stays put while segment 7's base moves every frame -- with
// linear ordering and the translation always interpolated, so a paired part never
// decides on its own to snap. The torso has no bone matrix of its own; a
// multiply by identity under the torso's id at the start of the model gives it
// one. After the model RT64's defaults are put back. BH_NO_MODEL_IDS=1: off.
constexpr uint32_t kAdamModel = 0x010031E0u;
constexpr uint32_t kBlackAdamModel = 0x050408F0u;
constexpr uint32_t kIdentityMtx = 0x00780000u;   // scratch (PORTING.md, scratch RDRAM)
constexpr uint32_t kTorsoBone = 0x07FFFFC0u;     // the torso's id key: no real bone address

bool model_ids_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BH_NO_MODEL_IDS");
        return !(v != nullptr && *v != '\0' && *v != '0');
    }();
    return on;
}

uint32_t model_id(uint32_t model, uint32_t bone_segmented) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t v : { bone_segmented, model, 0x4144414Du }) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 0x100000001B3ull;
        }
    }
    // Top bit set: never G_EX_ID_IGNORE (0); never G_EX_ID_AUTO (~0).
    const uint32_t id = static_cast<uint32_t>(h ^ (h >> 32)) | 0x80000000u;
    return id == G_EX_ID_AUTO ? 0xFFFFFFFEu : id;
}

void write_identity(uint8_t* rdram) {
    // Fixed-point Mtx: eight words of integer halves, eight of fractions.
    const uint32_t words[16] = { 0x00010000u, 0, 1, 0, 0, 0x00010000u, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    std::memcpy(rdram + kIdentityMtx, words, sizeof words);
}

// ---- the player's shadow -------------------------------------------------------
//
// The shadow under the player is a five-vertex quad (four corners and a centre at
// his x,z) whose vertices the game computes in world coordinates every frame and
// draws under the world matrix every shadow shares (decomp F7870.c
// func_800E988C). That matrix never moves, so RT64 pairs the transform as still
// and the shadow steps at the game's 20 frames while the player glides
// (Daniel, after the model fix; the Wave Race sky/water pattern, playbook 09).
// The quad is always the same five points in the same order, so its vertices
// are interpolated by index: an identity multiply opens a transform of its own
// under an explicit id with vertex interpolation and linear ordering, and another
// closes it after its triangles. It is found as the five-vertex quad whose centre
// is near the player's position (the instance D_80052B34 points at), which also
// follows him into a vehicle. A move of more than kShadowTeleport units in one game frame
// is drawn at its new place. BH_NO_SHADOW_INTERP=1: off.
constexpr uint32_t kPlayerInstancePtr = 0x00052B34u;
constexpr uint32_t kShadowId = 0x5348444Fu;          // "SHDO": top bit clear, not IGNORE/AUTO, not a model id
constexpr int kShadowTeleport = 400;
constexpr int kShadowNear = 96;

bool shadow_interp_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BH_NO_SHADOW_INTERP");
        return !(v != nullptr && *v != '\0' && *v != '0');
    }();
    return on;
}

// ---- the aiming reticle -----------------------------------------------------------
//
// The same shape as the shadow (Daniel: "It jitters when I aim around"). The
// reticle is a camera-facing billboard of nine vertices around the aim point --
// a centre, four corners and four edge midpoints -- computed in world coordinates
// every frame (decomp AAA70.c func_800A2D98 / func_800A2260, the "ghost" copy in
// func_800A2B58 draws the same vertices fainter) and drawn under the static world
// matrix 0x80031160. It is found by its texture, SETTIMG 0x01009A70, followed by
// a nine-vertex load, and each occurrence in a frame is given its own id in
// order. A centre that moves more than kReticleTeleport world units in one game
// frame -- a new target -- is drawn at its new place. BH_NO_RETICLE_INTERP=1: off.
constexpr uint32_t kReticleTexture = 0x01009A70u;
constexpr uint32_t kReticleId = 0x52544330u;          // "RTC0" + occurrence
constexpr int kReticleTeleport = 2000;
constexpr int kReticleMax = 4;

bool reticle_interp_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BH_NO_RETICLE_INTERP");
        return !(v != nullptr && *v != '\0' && *v != '0');
    }();
    return on;
}

// Whether the game-side HUD wrappers' regions are applied (BH_NO_HUD_ANCHORS=1: off;
// the wrappers are not registered then either).
bool anchors_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BH_NO_HUD_ANCHORS");
        return !(v != nullptr && *v != '\0' && *v != '0');
    }();
    return on;
}

int class_of(const std::string& identity) {
    return bh::inspector::class_for(identity.c_str());
}

// BH_HUD_REWRITE_TRACE=1: each identity the rewriter sees, once, with its class
// and where it was met (rect in a list, or a call).
void trace_seen(const std::string& identity, const char* where, int cls) {
    static const bool on = std::getenv("BH_HUD_REWRITE_TRACE") != nullptr;
    if (!on) return;
    static std::vector<std::string> seen;
    for (const auto& s : seen) if (s == identity) return;
    if (seen.size() > 200) return;
    seen.push_back(identity);
    std::fprintf(stderr, "[bh-hud] %s %s class %d\n", where, identity.c_str(), cls);
    std::fflush(stderr);
}

struct Writer {
    uint8_t* rdram;
    uint32_t base, size;
    uint32_t used = 0;
    bool overflow = false;

    uint32_t segments[16] = {};
    uint32_t fb_width = 320;
    bool have_viewport = false, have_scissor = false, have_projection = false;
    uint32_t viewport_w0 = 0, viewport_w1 = 0, scissor_w0 = 0, scissor_w1 = 0;
    uint32_t projection_w0 = 0, projection_w1 = 0;
    uint32_t fill_colour = 0;
    std::string texture_ident;
    int applied = 0;
    uint32_t model = 0;         // the player model being copied, or 0
    int models = 0;
    bool have_player = false;
    int16_t player_x = 0, player_z = 0;
    bool shadow_open = false;   // a vertex-interpolated group (shadow or reticle) is open
    int shadows = 0;
    bool shadow_teleported = false;
    uint32_t texture_image = 0; // w1 of the last SETTIMG
    int reticles = 0;
    int reticle_centres[kReticleMax][3] = {};   // this frame's, by occurrence

    // Whether a vertex load is the reticle billboard; records its centre.
    bool is_reticle(uint32_t w0, uint32_t w1) {
        if (texture_image != kReticleTexture || ((w0 >> 10) & 0x3F) != 9 || ((w0 >> 17) & 0x7F) != 0) return false;
        if (reticles >= kReticleMax) return false;
        const uint32_t at = physical(w1) + 4 * 16;   // vertex 4: the aim point
        const uint32_t a = read_word(rdram, at), b = read_word(rdram, at + 4);
        reticle_centres[reticles][0] = static_cast<int16_t>(a >> 16);
        reticle_centres[reticles][1] = static_cast<int16_t>(a);
        reticle_centres[reticles][2] = static_cast<int16_t>(b >> 16);
        return true;
    }

    // Vertex interpolation for the shadow; G_EX_ID_AUTO puts RT64's defaults back.
    void shadow_group(uint32_t id, bool interpolate_vertices) {
        const bool on = id != G_EX_ID_AUTO;
        const uint32_t v = (on && interpolate_vertices) ? G_EX_COMPONENT_INTERPOLATE : G_EX_COMPONENT_SKIP;
        if (GfxCommand* cmd = reserve(2)) {
            gEXMatrixGroup(cmd, id, G_EX_INTERPOLATE_DECOMPOSE, G_EX_NOPUSH, 0,
                           G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                           G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, v, G_EX_COMPONENT_AUTO,
                           on ? G_EX_ORDER_LINEAR : G_EX_ORDER_AUTO, G_EX_EDIT_NONE,
                           G_EX_ASPECT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
        }
        emit((static_cast<uint32_t>(kMtx) << 24) | 0x40u, 0x80000000u | kIdentityMtx);
    }

    // Whether a vertex load is the player's shadow quad: five vertices, the fifth
    // the centre of the four corners, near the player. Near, not at: by the time
    // the list is submitted the game has moved him on, and walking the centre was
    // measured 10-14 units behind the position read here.
    bool is_player_shadow(uint32_t w0, uint32_t w1) const {
        if (!have_player || ((w0 >> 10) & 0x3F) != 5 || ((w0 >> 17) & 0x7F) != 0) return false;
        const uint32_t at = physical(w1);
        int sx = 0, sz = 0;
        for (int i = 0; i < 4; ++i) {
            sx += static_cast<int16_t>(read_word(rdram, at + 16 * i) >> 16);
            sz += static_cast<int16_t>(read_word(rdram, at + 16 * i + 4) >> 16);
        }
        const int cx = static_cast<int16_t>(read_word(rdram, at + 64) >> 16);
        const int cz = static_cast<int16_t>(read_word(rdram, at + 68) >> 16);
        return std::abs(sx / 4 - cx) <= 2 && std::abs(sz / 4 - cz) <= 2 &&
               std::abs(cx - player_x) < kShadowNear && std::abs(cz - player_z) < kShadowNear;
    }

    // RT64's defaults in every field but the id, the ordering and the translation;
    // with G_EX_ID_AUTO it puts the defaults back entirely (Wave Race 64).
    void model_group(uint32_t id) {
        const bool on = id != G_EX_ID_AUTO;
        if (GfxCommand* cmd = reserve(2)) {
            gEXMatrixGroup(cmd, id, G_EX_INTERPOLATE_DECOMPOSE, G_EX_NOPUSH, 0,
                           on ? G_EX_COMPONENT_INTERPOLATE : G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                           G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                           G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO,
                           on ? G_EX_ORDER_LINEAR : G_EX_ORDER_AUTO, G_EX_EDIT_NONE,
                           G_EX_ASPECT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
        }
    }

    uint32_t physical(uint32_t address) const {
        if ((address >> 24) >= 0x80) return address & 0x1FFFFFFF;
        return (segments[(address >> 24) & 0x0F] + (address & 0x00FFFFFF)) & 0x1FFFFFFF;
    }

    GfxCommand* reserve(uint32_t count) {
        if (used + 8 * count > size) {
            overflow = true;
            return nullptr;
        }
        GfxCommand* cmd = reinterpret_cast<GfxCommand*>(rdram + base + used);
        used += 8 * count;
        return cmd;
    }
    void emit(uint32_t w0, uint32_t w1) {
        if (GfxCommand* cmd = reserve(1)) {
            cmd->values.word0 = w0;
            cmd->values.word1 = w1;
        }
    }
    void enable() {
        if (GfxCommand* cmd = reserve(1)) gEXEnable(cmd);
    }

    // RT64 displaces an aligned coordinate by origin / 1024 of the framebuffer's
    // width, in quarter pixels; this undoes it, so the game's own numbers are
    // measured from the chosen edge (Wave Race 64).
    int origin_cancel(uint32_t origin) const {
        return -static_cast<int>((origin * fb_width * 4) / G_EX_ORIGIN_RIGHT);
    }

    // A scissor spanning the widened frame with the game's vertical bounds.
    void widen_scissor() {
        if (!have_scissor) return;
        const uint8_t mode = static_cast<uint8_t>((scissor_w1 >> 24) & 3);
        const int ulx = static_cast<int>((scissor_w0 >> 12) & 0xFFF) >> 2;
        const int uly = static_cast<int>(scissor_w0 & 0xFFF) >> 2;
        const int lrx = static_cast<int>((scissor_w1 >> 12) & 0xFFF) >> 2;
        const int lry = static_cast<int>(scissor_w1 & 0xFFF) >> 2;
        if (GfxCommand* cmd = reserve(2)) {
            gEXSetScissor(cmd, mode, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, ulx, uly,
                          lrx - static_cast<int>(fb_width), lry);
        }
    }
    void restore_scissor() {
        if (have_scissor) emit(scissor_w0, scissor_w1);
    }

    void projection_group(uint32_t aspect) {
        if (GfxCommand* cmd = reserve(2)) {
            gEXMatrixGroup(cmd, G_EX_ID_AUTO, G_EX_INTERPOLATE_SIMPLE, G_EX_NOPUSH, 1,
                           G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                           G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                           G_EX_COMPONENT_SKIP, G_EX_ORDER_AUTO, G_EX_EDIT_NONE, aspect,
                           G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
        }
        if (have_projection) emit(projection_w0, projection_w1);
    }

    void viewport_align(uint32_t origin, int offset) {
        if (GfxCommand* cmd = reserve(2)) gEXSetViewportAlign(cmd, origin, offset, 0);
        if (have_viewport) emit(viewport_w0, viewport_w1);
    }

    // ---- anchor regions (markers from the game-side HUD wrappers) ----
    int anchor = bh::inspector::kAuto;   // the open region's class
    int anchors = 0;

    static uint32_t origin_of(int cls) {
        return cls == bh::inspector::kRight ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT;
    }
    void anchor_begin(int cls) {
        widen_scissor();
        const uint32_t origin = origin_of(cls);
        viewport_align(origin, origin_cancel(origin));
        ++anchors;
    }
    void anchor_end() {
        viewport_align(G_EX_ORIGIN_NONE, 0);
        restore_scissor();
    }
    // A rectangle inside a region takes the region's edge.
    void anchor_rect_begin() {
        const uint32_t origin = origin_of(anchor);
        const int off = origin == G_EX_ORIGIN_RIGHT ? origin_cancel(G_EX_ORIGIN_RIGHT) : 0;
        if (GfxCommand* cmd = reserve(2)) gEXSetRectAlign(cmd, origin, origin, off, 0, off, 0);
    }
    void anchor_rect_end() {
        if (GfxCommand* cmd = reserve(2)) gEXSetRectAlign(cmd, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    }

    // ---- rectangles ----
    void rect_begin(int cls) {
        switch (cls) {
            case bh::inspector::kLeft:
                widen_scissor();
                if (GfxCommand* cmd = reserve(2)) gEXSetRectAlign(cmd, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
                break;
            case bh::inspector::kRight: {
                widen_scissor();
                const int off = origin_cancel(G_EX_ORIGIN_RIGHT);
                if (GfxCommand* cmd = reserve(2)) gEXSetRectAlign(cmd, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, off, 0, off, 0);
                break;
            }
            case bh::inspector::kStretch:
                if (GfxCommand* cmd = reserve(1)) gEXSetRectAspect(cmd, G_EX_ASPECT_STRETCH);
                break;
            case bh::inspector::kSpill:
                widen_scissor();
                break;
            default:
                break;
        }
    }
    void rect_end(int cls) {
        switch (cls) {
            case bh::inspector::kLeft:
            case bh::inspector::kRight:
                if (GfxCommand* cmd = reserve(2)) gEXSetRectAlign(cmd, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
                restore_scissor();
                break;
            case bh::inspector::kStretch:
                if (GfxCommand* cmd = reserve(1)) gEXSetRectAspect(cmd, G_EX_ASPECT_AUTO);
                break;
            case bh::inspector::kSpill:
                restore_scissor();
                break;
            default:
                break;
        }
    }

    // ---- triangle groups (a called list) ----
    void group_begin(int cls) {
        switch (cls) {
            case bh::inspector::kLeft:
                widen_scissor();
                viewport_align(G_EX_ORIGIN_LEFT, origin_cancel(G_EX_ORIGIN_LEFT));
                break;
            case bh::inspector::kRight:
                widen_scissor();
                viewport_align(G_EX_ORIGIN_RIGHT, origin_cancel(G_EX_ORIGIN_RIGHT));
                break;
            case bh::inspector::kStretch:
                projection_group(G_EX_ASPECT_STRETCH);
                break;
            case bh::inspector::kSpill:
                widen_scissor();
                break;
            default:
                break;
        }
    }
    void group_end(int cls) {
        switch (cls) {
            case bh::inspector::kLeft:
            case bh::inspector::kRight:
                viewport_align(G_EX_ORIGIN_NONE, 0);
                restore_scissor();
                break;
            case bh::inspector::kStretch:
                projection_group(G_EX_ASPECT_AUTO);
                break;
            case bh::inspector::kSpill:
                restore_scissor();
                break;
            default:
                break;
        }
    }

    // Copies the list at `address` (and, recursively, the lists it calls) into
    // the scratch buffer and returns the copy's KSEG0 address.
    uint32_t copy_list(uint32_t address, int depth) {
        const uint32_t start = used;
        int branch_cls = bh::inspector::kAuto;   // class of the branch target being inlined
        enable();   // RT64 forgets the extended GBI at the end of every list
        uint32_t pc = physical(address);
        for (int guard = 0; guard < 40000 && !overflow; ++guard) {
            if (pc >= 0x800000) break;
            const uint32_t w0 = read_word(rdram, pc);
            const uint32_t w1 = read_word(rdram, pc + 4);
            const uint8_t op = static_cast<uint8_t>(w0 >> 24);
            pc += 8;
            if (shadow_open && op != kTri1 && op != kTri2 && op != kQuad) {
                shadow_group(G_EX_ID_AUTO, false);
                shadow_open = false;
            }
            switch (op) {
                case kVtx:
                    if (!shadow_open && model == 0 && shadow_interp_enabled() && is_player_shadow(w0, w1)) {
                        shadow_group(kShadowId, !shadow_teleported);
                        shadow_open = true;
                        ++shadows;
                    }
                    else if (!shadow_open && model == 0 && reticle_interp_enabled() && is_reticle(w0, w1)) {
                        static int previous[kReticleMax][3] = {};
                        static bool had_previous[kReticleMax] = {};
                        const int k = reticles;
                        const int* c = reticle_centres[k];
                        const bool jumped = !had_previous[k] ||
                            std::abs(c[0] - previous[k][0]) + std::abs(c[1] - previous[k][1]) +
                            std::abs(c[2] - previous[k][2]) > kReticleTeleport;
                        previous[k][0] = c[0]; previous[k][1] = c[1]; previous[k][2] = c[2];
                        had_previous[k] = true;
                        shadow_group(kReticleId + static_cast<uint32_t>(k), !jumped);
                        shadow_open = true;
                        ++reticles;
                    }
                    emit(w0, w1);
                    break;
                case kEndDl:
                    if (depth == 0 && anchor != bh::inspector::kAuto) {
                        anchor_end();   // a region the game never closed
                        anchor = bh::inspector::kAuto;
                    }
                    if (branch_cls != bh::inspector::kAuto) group_end(branch_cls);
                    emit(w0, w1);
                    return 0x80000000u | (base + start);
                case kDl: {
                    const bool branch = ((w0 >> 16) & 1) != 0;
                    if (branch) {
                        // A branch never returns: its list's end command is this
                        // list's end (Hybrid Heaven). A classified branch target is
                        // wrapped from here to that end command.
                        const std::string id = bh::hudid::list(rdram, w1, physical(w1));
                        const int cls = class_of(id);
                        trace_seen(id, "branch", cls);
                        if (branch_cls != bh::inspector::kAuto) group_end(branch_cls);
                        branch_cls = cls;
                        if (cls != bh::inspector::kAuto) {
                            group_begin(cls);
                            ++applied;
                        }
                        pc = physical(w1);   // follow it inline
                        break;
                    }
                    const std::string id = bh::hudid::list(rdram, w1, physical(w1));
                    const int cls = depth < 10 ? class_of(id) : bh::inspector::kAuto;
                    trace_seen(id, "call", cls);
                    uint32_t callee = w1;
                    const bool player = model == 0 && depth < 10 && model_ids_enabled() &&
                                        (w1 == kAdamModel || w1 == kBlackAdamModel);
                    if (player) {
                        model = w1;
                        ++models;
                        // The torso's own transform: identity, under the torso's id.
                        model_group(model_id(model, kTorsoBone));
                        emit((static_cast<uint32_t>(kMtx) << 24) | 0x40u, 0x80000000u | kIdentityMtx);
                    }
                    if (depth < 10) {
                        // Reserve a jump over the callee's copy.
                        const uint32_t jump_at = used;
                        emit(0, 0);
                        uint32_t copied = copy_list(w1, depth + 1);
                        if (overflow) return 0;
                        const uint32_t after = used;
                        // Rewrite the placeholder as a no-push branch to `after`.
                        const uint32_t jw0 = (static_cast<uint32_t>(kDl) << 24) | (1u << 16);
                        const uint32_t jw1 = 0x80000000u | (base + after);
                        std::memcpy(rdram + base + jump_at, &jw0, 4);
                        std::memcpy(rdram + base + jump_at + 4, &jw1, 4);
                        callee = copied;
                    }
                    group_begin(cls);
                    emit(w0, callee);
                    enable();
                    group_end(cls);
                    if (cls != bh::inspector::kAuto && anchor != bh::inspector::kAuto) {
                        // The call's own class ended its alignment; the region's resumes.
                        widen_scissor();
                        viewport_align(origin_of(anchor), origin_cancel(origin_of(anchor)));
                    }
                    if (player) {
                        model_group(G_EX_ID_AUTO);
                        model = 0;
                    }
                    if (cls != bh::inspector::kAuto) ++applied;
                    break;
                }
                case kMoveWord:
                    if ((w0 & 0xFF) == kMwSegment) {
                        segments[(w0 >> 10) & 0x0F] = w1 & 0x1FFFFFFF;
                    }
                    emit(w0, w1);
                    break;
                case kMoveMem:
                    if (((w0 >> 16) & 0xFF) == kMvViewport) {
                        have_viewport = true;
                        viewport_w0 = w0;
                        viewport_w1 = w1;
                    }
                    emit(w0, w1);
                    break;
                case kMtx:
                    if ((((w0 >> 16) & 0xFF) & kMtxProjection) != 0) {
                        have_projection = true;
                        projection_w0 = w0;
                        projection_w1 = w1;
                    }
                    else if (model != 0 && (w1 >> 24) == 0x07) {
                        // A bone of the player model: its own id (see model_id above).
                        model_group(model_id(model, w1));
                    }
                    emit(w0, w1);
                    break;
                case kSetScissor:
                    have_scissor = true;
                    scissor_w0 = w0;
                    scissor_w1 = w1;
                    emit(w0, w1);
                    if (anchor != bh::inspector::kAuto) widen_scissor();
                    break;
                case kRdpNoop:
                    if ((w1 & 0xFFFFFF00u) == kAnchorMagic) {
                        const int cls = static_cast<int>(w1 & 0xFF);
                        if (cls != anchor) {
                            if (anchor != bh::inspector::kAuto) anchor_end();
                            anchor = (cls == bh::inspector::kLeft || cls == bh::inspector::kRight) ? cls : bh::inspector::kAuto;
                            if (anchor != bh::inspector::kAuto && anchors_enabled()) anchor_begin(anchor);
                            else anchor = bh::inspector::kAuto;
                        }
                        break;   // the marker itself is not passed on
                    }
                    emit(w0, w1);
                    break;
                case kSetCImg:
                    fb_width = (w0 & 0xFFF) + 1;
                    emit(w0, w1);
                    break;
                case kSetTImg:
                    texture_ident = bh::hudid::texture(rdram, w1, physical(w1));
                    texture_image = w1;
                    emit(w0, w1);
                    break;
                case kSetFillColor:
                    fill_colour = w1;
                    emit(w0, w1);
                    break;
                case kFillRect: {
                    // Same coordinates as the panel's feed: 320x240, clears excluded.
                    const float to_320 = 320.0f / static_cast<float>(fb_width);
                    const std::string id = bh::hudid::fill(
                        fill_colour, int((((w1 >> 12) & 0xFFF) / 4.0f) * to_320), int(((w1 & 0xFFF) / 4.0f) * to_320),
                        int((((w0 >> 12) & 0xFFF) / 4.0f) * to_320), int(((w0 & 0xFFF) / 4.0f) * to_320));
                    const int cls = id.empty() ? bh::inspector::kAuto : class_of(id);
                    if (!id.empty()) trace_seen(id, "fill rect", cls);
                    if (cls == bh::inspector::kAuto && anchor != bh::inspector::kAuto) {
                        anchor_rect_begin();
                        emit(w0, w1);
                        anchor_rect_end();
                        break;
                    }
                    rect_begin(cls);
                    emit(w0, w1);
                    rect_end(cls);
                    if (anchor != bh::inspector::kAuto) widen_scissor();
                    if (cls != bh::inspector::kAuto) ++applied;
                    break;
                }
                case kTexRect:
                case kTexRectFlip: {
                    const int cls = class_of(texture_ident);
                    trace_seen(texture_ident, "tex rect", cls);
                    const bool by_region = cls == bh::inspector::kAuto && anchor != bh::inspector::kAuto;
                    if (by_region) anchor_rect_begin();
                    else rect_begin(cls);
                    emit(w0, w1);
                    // RT64's HLE texrect consumes the next two commands whatever they are.
                    for (int i = 0; i < 2; ++i) {
                        emit(read_word(rdram, pc), read_word(rdram, pc + 4));
                        pc += 8;
                    }
                    if (by_region) {
                        anchor_rect_end();
                        break;
                    }
                    rect_end(cls);
                    if (anchor != bh::inspector::kAuto) widen_scissor();
                    if (cls != bh::inspector::kAuto) ++applied;
                    break;
                }
                default:
                    emit(w0, w1);
                    break;
            }
        }
        // Ran off the end or out of room: not safe to submit.
        overflow = true;
        return 0;
    }
};

int g_turn = 0;

}  // namespace

uint32_t rewrite(uint8_t* rdram, uint32_t list_address) {
    static const bool off = [] {
        const char* v = std::getenv("BH_NO_HUD_REWRITE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    if (off || (!bh::inspector::any_classes() && !model_ids_enabled() && !anchors_enabled())) return 0;

    write_identity(rdram);
    g_turn ^= 1;
    Writer w{ rdram, kScratch[g_turn], kScratchSize };

    // The player's position, for finding his shadow (see shadow_interp_enabled).
    const uint32_t player = read_word(rdram, kPlayerInstancePtr);
    if ((player >> 24) == 0x80 && (player & 0x1FFFFFFF) < 0x7FFFF8) {
        w.have_player = true;
        w.player_x = static_cast<int16_t>(read_word(rdram, player & 0x1FFFFFFF) >> 16);
        w.player_z = static_cast<int16_t>(read_word(rdram, (player & 0x1FFFFFFF) + 4) >> 16);
        static int16_t last_x = 0, last_z = 0;
        static bool had_last = false;
        w.shadow_teleported = !had_last || std::abs(w.player_x - last_x) + std::abs(w.player_z - last_z) > kShadowTeleport;
        last_x = w.player_x;
        last_z = w.player_z;
        had_last = true;
    }
    const uint32_t copy = w.copy_list(list_address, 0);
    static bool reported_overflow = false;
    if (w.overflow || copy == 0) {
        if (!reported_overflow) {
            reported_overflow = true;
            std::fprintf(stderr, "[bh] HUD rewrite: a list did not fit or did not end (%u bytes used); submitted unchanged\n",
                         w.used);
            std::fflush(stderr);
        }
        return 0;
    }
    // BH_SHADOW_TRACE=1: in how many of the last 100 gameplay lists the shadow was found.
    static const bool shadow_trace = std::getenv("BH_SHADOW_TRACE") != nullptr;
    if (shadow_trace && w.have_player) {
        static int lists = 0, found = 0;
        ++lists;
        found += w.shadows > 0 ? 1 : 0;
        if (lists == 100) {
            std::fprintf(stderr, "[bh] player shadow found in %d of 100 lists\n", found);
            std::fflush(stderr);
            lists = found = 0;
        }
    }
    static bool reported_anchors = false;
    if (w.anchors > 0 && !reported_anchors) {
        reported_anchors = true;
        std::fprintf(stderr, "[bh] HUD widgets anchored to the edges: %d region(s) in this frame (BH_NO_HUD_ANCHORS=1: off)\n",
                     w.anchors);
        std::fflush(stderr);
    }
    static bool reported_reticle = false;
    if (w.reticles > 0 && !reported_reticle) {
        reported_reticle = true;
        std::fprintf(stderr, "[bh] aiming reticle: vertices interpolated, %d in this frame (BH_NO_RETICLE_INTERP=1: off)\n",
                     w.reticles);
        std::fflush(stderr);
    }
    static bool reported_shadow = false;
    if (w.shadows > 0 && !reported_shadow) {
        reported_shadow = true;
        std::fprintf(stderr, "[bh] player shadow: vertices interpolated (BH_NO_SHADOW_INTERP=1: off)\n");
        std::fflush(stderr);
    }
    static bool reported_model = false;
    if (w.models > 0 && !reported_model) {
        reported_model = true;
        std::fprintf(stderr, "[bh] player model: parts paired by identity (BH_NO_MODEL_IDS=1: off)\n");
        std::fflush(stderr);
    }
    static int reported = 0;
    if (w.applied > 0 && reported < 3) {
        ++reported;
        std::fprintf(stderr, "[bh] HUD rewrite: %d classified element draw(s) in a %u-byte copy\n", w.applied, w.used);
        std::fflush(stderr);
    }
    return copy;
}

}  // namespace bh::hudrewrite
