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

## Menus at 21:9 (after changes 1–3)

`--grab 17,23,32,38,47,55` with `start-spam.txt` at `BH_WINDOW_SIZE=1680x720`: title, name entry (three
frames), Play menu and the dropship cutscene. Every menu fills the window — backdrop stretched, the 3D
ship behind the dimmed menus widened, buttons and letters at their own size in the middle. The dropship
cutscene is widened with the game's own cinematic bands at the top and bottom.

## Change 4 — the sky strip stretched (`BH_NO_SKY_STRETCH`)

**Reported by Daniel after the first build:** "the skybox gets cut-off ... it should span the whole
screen (spill or cover)". In a wide window the sky picture covered only the middle 4:3 of the top of the
screen; beside it, nothing (black or a flat colour block).

**Measured** (`BH_DL_CENSUS`, now printing s,t, dsdx,dtdy and the colour image per rectangle): after the
depth clear, gameplay fills the sky colour from the horizon down (`fill 0,33..319,239`), then draws the
panorama as 2D texture rectangles:

- 32×32 tiles at dsdx 0.5, dtdy 0.666, so 64 px wide and 48 lines tall;
- their images sit on the heap, 0x400 apart, and change as the camera turns;
- the first and last tile of a row are cut by the yaw (e.g. x 0..17, 17..81 … 273..319);
- the tile above the horizon is cut by the pitch (t 10, y 0..33).

As 2D they are placed in the 4:3 middle.

**Wrong turns kept:**
- `drawSky?` in the decomp (`func_80070CC0`) draws the cinematic bands, not the sky. No other
  function in the C or the asm could be tied to these commands by their constants.
- Extending the panorama past the edges was considered, which needs the tile count to wrap. The texture's
  MIO0 size is not a multiple of a tile (tiles are built at run time), and a turn test saw 40 images
  but never a wrap.
- Daniel meanwhile set the six visible tiles to `stretch` in the F1 panel of a test window. That works for
  that view only: the identities change with the camera.

**First version, partial:** `src/dlcensus.cpp` looks for a row of rectangles with equal top and bottom
edges joining end to end from x 0 to 319 (tested on the rectangles one by one, not the panel's merged
elements), and gives the whole row `stretch`. 16:9 grabs looking ahead: filled. Daniel, aiming at the
sky: still 4:3 below the top band. His live session's census showed why:
- the panorama is a **grid**, 10 columns by 4 rows of images in Greece (`0x802CA8D0`, `0x802CD0D0`,
  `0x802CF8D0`, `0x802D20D0` start the rows);
- looking up, the rows stack down the screen (y 0..4, 4..52, 52..100, 100..148 …);
- the rule stopped at the first row it found.

**Fix:** every full-width row in the frame is stretched. Confirmed by Daniel aiming at the sky, 16:9.

## HUD anchoring

**First, the panel:** Daniel tagged the weapon icon, ammo box and ammo digits `left` and saved
`hud.json`. They were promoted into `load_defaults()` (`tools/promote_hud_tags.py`, commit `708b7a1`).

**Reported next:** "I cannot find the other HUD elements to anchor: the map/radar should be anchored to
the right, the health and other bar should be anchored left."

**Survey** (every 2D rectangle and element of a gameplay frame, one entry each):

| Element | Drawn as | Identity |
|---|---|---|
| Radar | triangles directly in the top-level list, x 242–309, y 10–78 | `tex:0x0502e110`, `0x0014ede0`, `0x0503c8b0` |
| Health / alien bar frames | rectangles at x 25–92, y 182–196 and 202–216, **and the same textures at x 982–997** | `tex:0x01011c80` … `0x01013380` |
| Bar fills | top-level triangles, merged extent x −38…88 | `tex:0x8013d540` |
| Heart / face icons | top-level triangles, x 21–39 | `tex:0x8025f540`, `0x8025f780` |

Two reasons the panel could not do it:
- The rewriter applied classes to rectangles and called lists only, never to top-level triangle runs.
- One texture class cannot separate the left bars from the vehicle's bars on the right:
  `func_8009C6CC_AB67C(x, y, fraction, side, icon, …)` draws both, side 0 at x 0x50 and side 1 at
  width − 0x20 (decomp AAA70.c `func_8009D96C`).

**Fix, game side** (`src/widescreen.cpp`): three draw functions are wrapped, re-registered on each
outside-overlay load. Each writes an RDP `G_NOOP` marker (`0xC0000000, 0x42484D00 | class`) into the
game's display list before the call and an end marker after it:
- `func_8009C6CC_AB67C` (bars): left or right from its `side` argument;
- `func_800A03FC_AF3AC` (DisplayScanner, the radar): right;
- `func_8013A764_149714` (weapon and ammo): left.

**Fix, rewriter** (`src/hudrewrite.cpp`), for everything between a start and end marker:
- the viewport is aligned to that edge (triangles and called lists) and the scissor widened;
- each rectangle gets the edge's rect origin;
- an identity with a class of its own (panel, `hud.json`, built-in) still takes that class, and the
  region's alignment resumes after it;
- markers are not passed on.

`BH_NO_HUD_ANCHORS=1` removes both halves.

**Verified:** log `HUD widgets anchored to the edges`; Daniel watching the 16:9 test run: "That worked".

## Not yet done

- HUD anchoring beyond the radar, bars and weapon panel (e.g. dialogue boxes, scanner text): none requested yet.
- ~~Whether the cutscene's top/bottom bands should stay~~ **Decided by Daniel (2026-09-15): keep
  them.** They are the game's own cinematic letterbox (the "no black bars" exception for an original
  letterbox).
- ~~Buildings (`inside` overlay)~~ **done 2026-09-16** (next section); vehicles' own culls at wide aspects.

## Change — the interior cull (`BH_NO_WIDE_INSIDE_CULL`)

**Symptom (Daniel, first Greece interior, 16:9):** "Any item that is too far away goes invisible"; in the
PrintWindow grab the left wall was missing, with background in its place, while the right wall showed.

**Measured / read:** the outdoor fix does not reach buildings. Every cell and object draw in
`inside/158330.c` calls `func_8007C428_1644E8`; its recompiled MIPS (the decomp's C is `NON_MATCHING` and
has the wrong arity) shows a 50° cone from the 9th argument and a hard `slti 0x3C1` distance limit
(GAME-INTERNALS.md "Culling inside buildings").

**Change:** native copy of the test; widened cone and a room-diagonal limit only for points the game
rejects, under a matrix-pool/display-list headroom check (PORTING.md).

**Result:** Daniel: "That fixed it." Not measured: the `BH_INSIDE_CULL_TRACE` counts (log locked while the
game ran), the per-frame cost, and larger interiors near the matrix budget.
