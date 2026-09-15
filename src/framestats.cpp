// The game's frame rate beside the rate RT64 presents at, and how well RT64 pairs
// transforms for interpolation.
//
// osViSwapBuffer (runtime-provided, cartridge address 0x8001EDF0) is called once
// per frame the game finishes, so counting it over a two-second window is the
// game's own rate: 60 in menus, 20 in outdoor gameplay (docs/GAME-INTERNALS.md,
// Timing). Beside it, the rate RT64 presents at says whether the Framerate
// setting took. BH_FRAME_STATS=1 prints every window; otherwise a line only when
// either rate changes.
//
// BH_PAIRING=1 adds RT64's transform-pairing counters over the same window
// (tools/patch_rt64_pairing.py): transforms per frame, those not interpolated by
// request, and those left unpaired (and of those, moved or new). Modelled on
// Pilotwings 64: Recompiled's src/patch_host.cpp.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/config.hpp>

extern "C" void osViSwapBuffer_recomp(uint8_t* rdram, recomp_context* ctx);

extern "C" {
// Added to RT64 by tools/patch_rt64_pairing.py.
void RT64_GetTransformPairing(unsigned long long* frames, unsigned long long* total, unsigned long long* ignored,
                              unsigned long long* unpaired, unsigned long long* unpaired_moved);
}

namespace {

constexpr uint32_t kOsViSwapBuffer = 0x8001EDF0;

void swap_buffer_counted(uint8_t* rdram, recomp_context* ctx) {
    osViSwapBuffer_recomp(rdram, ctx);

    using clock = std::chrono::steady_clock;
    static clock::time_point window_start = clock::now();
    static uint32_t frames = 0;
    static int last_rate = -1;
    static uint32_t last_presented = 0;

    ++frames;
    const auto elapsed = clock::now() - window_start;
    if (elapsed < std::chrono::seconds(2)) {
        return;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const int rate = static_cast<int>(std::lround(frames / seconds));
    window_start = clock::now();
    frames = 0;

    static const bool pairing = std::getenv("BH_PAIRING") != nullptr;
    if (pairing) {
        unsigned long long p_frames = 0, p_total = 0, p_ignored = 0, p_unpaired = 0, p_moved = 0;
        RT64_GetTransformPairing(&p_frames, &p_total, &p_ignored, &p_unpaired, &p_moved);
        static unsigned long long l_frames = 0, l_total = 0, l_ignored = 0, l_unpaired = 0, l_moved = 0;
        const unsigned long long d_frames = p_frames - l_frames;
        if (d_frames > 0) {
            std::fprintf(stderr,
                         "[bh] interpolation: %.0f transforms a frame, %.1f not interpolated by request,"
                         " %.1f unpaired (%.1f of them moved or new), %llu frames\n",
                         double(p_total - l_total) / d_frames, double(p_ignored - l_ignored) / d_frames,
                         double(p_unpaired - l_unpaired) / d_frames, double(p_moved - l_moved) / d_frames,
                         d_frames);
            std::fflush(stderr);
        }
        l_frames = p_frames;
        l_total = p_total;
        l_ignored = p_ignored;
        l_unpaired = p_unpaired;
        l_moved = p_moved;
    }

    static const bool every_window = [] {
        const char* value = std::getenv("BH_FRAME_STATS");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    const uint32_t presented = ultramodern::get_target_framerate(static_cast<uint32_t>(rate));
    if (rate == last_rate && presented == last_presented && !every_window) {
        return;
    }
    last_rate = rate;
    last_presented = presented;
    std::fprintf(stderr, "[bh] the game is running at %d frames per second; presenting at %u (display %u Hz)\n",
                 rate, presented, ultramodern::get_display_refresh_rate());
    std::fflush(stderr);
}

}  // namespace

namespace bh {

void install_frame_stats() {
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kOsViSwapBuffer), swap_buffer_counted);
}

}  // namespace bh
