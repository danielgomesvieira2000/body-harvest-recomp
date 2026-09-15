# Phase 07 findings — widescreen

**Gate:** in progress.

Reproduce with:

```
python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --rom rom.z64 --seconds 95 \
    --grab 14,70 --out <dir> --seed tools/test-seeds/graphics.json \
    --env BH_INPUT_SCRIPT=<repo>/tools/scripts/start-spam.txt --env BH_DL_CENSUS=300 --env BH_WINDOW_SIZE=1280x720
```

## Measurements before any change (`BH_DL_CENSUS=300`, Greece gameplay)

| What | Result |
|---|---|
| Top-level list | `0x801CE990`, ~4,800–5,400 commands, ~70 calls, 1,400–1,800 triangles per frame |
| Colour images | `0x003DA800` (an off-screen buffer, drawn first), then `0x00267080` |
| Viewport | scale 152,115 trans 152,115 → x 0..304, y 0..230 |
| Scissor | 0,0..304,230 |
| Clears | fill 0,0..303,229 (two colours) |
| Main 3D projection | `load persp aspect 1.3333 fovy 45.00 near 10.0 far 10015.5` |
| Other perspective | `aspect 1.3333 fovy 45 near 15 far 1280.2` (no triangles under it in these frames) |
| HUD | ortho projections (`sx 0.0125 sy -0.01666`, `sx 0.00658 sy -0.00868`), ~30 texture rectangles (weapon, health, alien bars at x 25..92, y 124..206) |
| 4:3 window | black strips at the right (~16/320) and bottom (~10/240) |
| 16:9 window | same strips; title screen pillarboxed (its star background is 4:3 texture rectangles); HUD at 4:3 positions |

## Change 1 — gameplay at 320×240 (`BH_FULL_FRAME`)

**Cause of the strips:** `setGameplayResolution()` (`0x80006D84`) sets the game's draw size to 304×230
(`setVideoInterfaceXSize(0x130)`, `setVideoInterfaceYSize(0xE6)`), and the game sets the VI's X/Y scale
(`osViSetXScale(304/320)`, `osViSetYScale(230/240)`) so the hardware stretches the region over the
screen. Menus use `setFullResolution()` (320×240). ultramodern's `osViSetXScale/YScale` are empty
(`events.cpp:545`, `assert` compiled out) and RT64's VI presentation reads no scale, so the unscaled
region shows.

**Options:** (a) implement VI scaling in the runtime and RT64's presentation (crop the presented
framebuffer); (b) snap viewport/scissor/fills at submission (Hybrid Heaven's shape); (c) make the game
use its own full-resolution path in gameplay.

**Chosen: (c)** — one function replaced at its address (`src/widescreen.cpp`): the game draws the whole
320×240 itself, every size-derived element (viewport, scissor, clears, text rows, radar offsets) follows,
and RT64 sees a 3D pass that covers the frame, which is what its Expand widening needs. (a) would keep the
304-wide pass that RT64's coverage tests reject for widening; (b) would miss the elements the game places
from the size.

**Result:** `BH_DL_CENSUS` shows viewport scale 160,120 trans 160,120, scissor 0..320×0..240, clears
0..319×0..239. The 4:3 grab fills the window; at 16:9 the view is wider (RT64 Expand) with no bars in
gameplay. `BH_FULL_FRAME=0` restores the game's region.

*Inferred cost:* elements at fixed coordinates sit 5 % / 4 % further from the right/bottom edges than the
VI stretch would have put them. To be judged in play.

## Change 2 — full-screen tile backdrops stretched (`BH_NO_BG_STRETCH`)

**Measured:** `BH_HUD_ELEMENTS_LOG=1` on the title: the star/planet backdrop is a grid of 32×32 texture
rectangles, ten columns (x 0..320), rows at y 176, 208 on the title and 192, 224 on the intro text
screen (`tex:0x0408xxxx`, `tex:0x0409xxxx`). As 2D they sit in the middle 4:3 of a wide window.

**Change:** `src/dlcensus.cpp` finds, per frame, texture rectangles 31–32.5 px square on 32-px columns and
16-px rows; if one row holds all ten columns, every such tile gets `stretch` through a new per-frame class
layer in the inspector (below the panel, `hud.json` and the built-in table). `src/hudrewrite.cpp` applies
it like any class.

**First attempt, wrong:** rows only on multiples of 32 — the intro's 224 row (and the title's bottom
strip) stayed 4:3. Fixed by allowing 16-px row starts.

**Result:** log `first full-screen tile background: 80 tiles stretched`; 16:9 grabs of the intro and title
fill the window: backdrop stretched, the 3D ship widened by RT64, logo and Start button at their own size
in the middle.

**Tool change:** `test_sandbox.py --grab` now uses `tools/print_window.py` (`PrintWindow` with
`PW_RENDERFULLCONTENT`). The GDI desktop grab photographed the terminal covering the game. The saved
bitmap is the DPI-virtualised client size (1600×900 for a 1280×720 window) with the picture in the
top-left; crop to the window size.

## Change 3 — the cull angle widened (`BH_NO_WIDE_CULL`, `BH_CULL_MARGIN`)

**Symptom (21:9, `BH_WINDOW_SIZE=1680x720`):** terrain tiles missing at both sides of the picture —
sawtooth edges with sky through them — while the camera was still.

**Found by reading, then confirmed by the change:** decomp `BF9C0.c`: `func_800B33BC_C236C(pitch)` stores
the full horizontal cull angle in `D_8014FD2A` (BAM) from a 33° half-angle (`D_80142E20 = 0.5759 rad`) and
the pitch; the terrain tile test `func_800B960C_C85BC` and the entity tests `func_800B93AC_C835C`,
`func_800B9228_C81D8` compare against it; `0x8000` disables them. (12feihu's port wrote `0x8000` there to
"render all entities".)

**Change:** `src/widescreen.cpp` wraps `func_800B33BC` (re-registered each time the outside overlay loads)
and replaces the stored angle with `2·atan(tan(game/2) · (aspect / (4/3)) · 1.10)`; aspect = window aspect
unless the aspect-ratio setting is Original.

**Result:** `cull angle 0x2EE0 -> 0x48F5 (aspect 2.333, margin 1.10)`; the same 21:9 frames show complete
terrain to both edges.

## Not yet done

- HUD anchoring (radar right, weapon/health/alien bars left): through the F1 panel with Daniel.
- Menus other than the title (slot select, name entry, Play menu) at 16:9 and 21:9 — grabbed only in passing.
- Buildings (`inside` overlay) and vehicles' own culls at wide aspects.
