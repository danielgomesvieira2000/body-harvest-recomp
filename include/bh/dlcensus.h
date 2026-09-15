#pragma once

// A read-only census of what one display list draws, and the HUD inspector's
// per-frame feed.
//
// BH_DL_CENSUS=<n> walks every n-th top-level display list the game submits,
// following calls and branches, and prints one report: the viewports, scissors
// and projection matrices it sets (with each perspective's aspect, vertical field
// of view and near/far planes), how many triangles it draws under each, and
// every fill and texture rectangle with its extent. Nothing in the list is
// changed. Unset, it costs one branch per list. This is phase 07's measuring
// instrument (playbook 08: measure the drawn region, the frustum and the 2D
// before building widescreen).
//
// F3DEX 1.x commands (include/bh/gbi_f3dex.h). Structure from Hybrid Heaven's
// src/dlcensus.cpp, written for F3DEX2.

#include <cstdint>

namespace bh::dlcensus {

// Whether a census is wanted for this list. Cheap; call for every list.
bool wanted();

// Walks the list at `list_address` (a KSEG0 or physical address) in `rdram`.
void run(const uint8_t* rdram, uint32_t list_address);

// The pass every submitted list gets: the HUD inspector's feed -- every 2D
// element of the frame (texture and fill rectangles, triangles under an
// orthographic projection) with its identity and its extent in 320x240 --
// unless BH_INSPECTOR=0.
void per_frame(uint8_t* rdram, uint32_t list_address);

}  // namespace bh::dlcensus
