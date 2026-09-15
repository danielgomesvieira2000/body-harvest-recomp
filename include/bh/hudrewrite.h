#pragma once

// Applies the HUD inspector's classes to the picture, live.
//
// While any 2D element has a class other than centre -- chosen in the F1 panel,
// or saved in hud.json -- each submitted display list is copied into scratch
// RDRAM the game does not use, and RT64's extended GBI is inserted around every
// classified element:
//
//   left / right   rectangles: gEXSetRectAlign to that edge; triangle groups:
//                  gEXSetViewportAlign and the game's viewport reissued. Both
//                  widen the scissor so the moved element is not cut at 4:3.
//   stretch        rectangles: gEXSetRectAspect(STRETCH); triangle groups: a
//                  projection group with G_EX_ASPECT_STRETCH, projection reissued.
//   spill          only the widened scissor.
//
// Identities are the panel's (include/bh/hudid.h). The emission follows Wave Race
// 64's dlrewrite and Hybrid Heaven's hudrewrite (playbook 08), with F3DEX 1.x
// command layouts (include/bh/gbi_f3dex.h).
//
// With no class set nothing is copied and the game's list goes to RT64 as it is.
// BH_NO_HUD_REWRITE=1 turns the copy off entirely for an A/B.

#include <cstdint>

namespace bh::hudrewrite {

// Returns the KSEG0 address of the rewritten list to submit instead, or 0 to
// submit the game's own.
uint32_t rewrite(uint8_t* rdram, uint32_t list_address);

}  // namespace bh::hudrewrite
