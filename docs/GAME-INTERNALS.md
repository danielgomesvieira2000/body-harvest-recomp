# Body Harvest internals

What the game turned out to be, for anyone working on this port, on the decompilation, or on another
port. Names are the decompilation's at the pinned revision (`lib/bh-decomp`, `4600677`), or
`func_XXXXXXXX_YYYYY` placeholders (vram, ROM offset). How the port deals with each fact is in
[PORTING.md](PORTING.md); how it was found is in [findings/](findings).

Conventions: every number names the tool or switch that measures it; refuted hypotheses are kept;
inference is marked *inferred*.

## Identity

| | |
|---|---|
| Title | Body Harvest (USA), game code `NBHE`, version 0 ("B6.5") |
| SHA-1 | `bbb6666f5014a473747ee4145f036d9fb25d7348` |
| XXH3-64 | `0x9949AA9BC66F33CB` (what librecomp checks) |
| Header CRC | `0x5326696F 0xFE9A99C3` |
| Size | 12 MB |
| Entry point | `0x80000400` |
| Save type | EEPROM 4 Kbit (`osEepromProbe` → 1), 3 save slots |
| Accessory | Rumble Pak (`osMotorInit/Start/Stop`) |
| Graphics microcode | F3DEX 1.21 and L3DEX 1.21 |
| Audio microcode | stock `aspMain`, text ROM `0x2FF10` (`0xE20`), data ROM `0x3FC60` |
| libultra | header `0x1448`; mix of 2.0I functions and 2.0E–G variants (decomp yaml notes) |

## Memory layout

| Region | ROM → VRAM | Notes |
|---|---|---|
| `core` | `0x1000` → `0x80000400` (`0x3F720`) | resident game code + libultra; bss from `0x8003FB20` |
| `overlay_gameplay_frontend` | `0x40720` → `0x80070270` (`0x3EB00`) | menus, title, name entry |
| `overlay_gameplay_outside` | `0x7F220` → `0x80070270` (`0xD9110`) | outdoor gameplay (terrain, aliens, vehicles, HUD, camera) |
| `overlay_gameplay_inside` | `0x158330` → `0x80070270` (`0x354B0`) | buildings |
| `overlay_level_{greece,java,america,siberia,comet}` | `0x18D7E0`, `0x1ED9E0`, `0x254410`, `0x2B7100`, `0x318E20` → `0x802D4CD0` | per-world code, followed by MIO0/RNC data and alien-type tables |
| framebuffers | `0x80267080`, `0x8028C880` | `BH_TRACE_FUNCS` on `osViSwapBuffer` |
| `osMemSize` | not referenced | *inferred* 4 MB game |

## Loading

`func_800101F0_10DF0(dest, rom, size)` → `func_8000FFC0_10BC0(&D_80067F70, dest, rom, size)`: DMAs
`0x800` bytes at a time with `osPiStartDma` and waits on `D_80067F70` after each. Callers:
`loadFrontendData`, `func_80011674_12274` (outside), `func_800117D8_123D8` (inside), `loadLevelCode(level)`
(tables `D_80031C18/2C/40`). Observed at boot (`BH_DEBUG_LOADS=1`): frontend overlay, then data
`0x4EBF80 → 0x802D4CD0`, `0x55B0A0 → 0x802E9750`, `0x43A340 → 0x80308400`, and the MIO0 icon/font blobs
from `0x3767C0` into `0x802B2080`; starting Greece loads `outside` and `level_greece`.

## Threads

| Thread (decomp) | Entry | Priority | Role |
|---|---|---|---|
| boot (`D_80067388`) | `func_8000EFB8_FBB8` | 10 → 0 | creates the others, then `osSetThreadPri(0,0); for(;;)` (idle) |
| graphics (`D_80067538`) | `func_8000F6B0_102B0` | 8 | scheduler client; builds/submits frames; 3 retraces per frame in gameplay, 1 in menus |
| game (`D_800676E8`) | `func_8000FE50_10A50` | 4 | waits for the save to be read (`D_8006A8F0`), loads the frontend, runs the game |
| controller (`D_80067898`) | `func_80002EF8_3AF8` | 5 | `osContInit`, EEPROM probe/read, then a tight `osContStartReadData` loop; rumble logic (`func_80001190_1D90`) runs per iteration |
| rumble (`D_800433C8`) | `func_80000ED4_1AD4` | 15 | `osMotorStart/Stop` commands from a queue |
| scheduler | libultra `osCreateScheduler` | — | `__scTaskReady` etc. at `0x8001B130`–`0x8001B940` |

Frame handshake: graphics sends `D_8006A8D0` (start a frame), game replies on `D_8006A8B0` (frame
built); retrace (type 1) and done (type 2) messages arrive on `D_8006A908` (8 slots).

## Timing

| Mode | Game frames/s | How |
|---|---|---|
| intro, menus | 60 | `BH_FRAME_STATS=1` |
| outdoor gameplay | 20 | `BH_FRAME_STATS=1`; decomp `FD80.c` `var_v1 = 3` |

## Rendering

- **Draw size:** `setFullResolution()` (`0x80006D5C`) = 320×240 for menus; `setGameplayResolution()`
  (`0x80006D84`) = 304×230 for gameplay, with `osViSetXScale/YScale` = size/320, size/240 so the VI
  stretches it. Viewport (`func_8000F368_FF68`: vscale = vtrans = size×2), scissor and clears derive from
  `D_80068084`/`D_80068088` (the current size). `BH_DL_CENSUS=300` in Greece: viewport 152,115, scissor
  0..304×0..230.
- **Projections in gameplay:** main 3D `persp aspect 1.3333 fovy 45 near 10 far 10015.5`; a second
  `fovy 45 near 15 far 1280.2`; HUD under ortho projections plus ~30 texture rectangles.
- **Culling:** `D_8014FD2A` = full horizontal cull angle (BAM, `0x8000` = off), set by
  `func_800B33BC_C236C(pitch)` from a 33° half-angle (`D_80142E20`); read by the terrain tile test
  `func_800B960C_C85BC` (camera-relative box, then angle) and entity tests `func_800B93AC_C835C`
  (angle + 4000 units), `func_800B9228_C81D8` (angle + 4500 units). `D_80157590 != 0` bypasses them
  (third-party port). Observed values: `0x2EE0`, `0x30C0`, `0x694C` with pitch.
- **Frontend backdrops:** grids of 32×32 texture rectangles covering 320 px (title rows at y 176/208,
  intro at 192/224).
- **Outdoor sky:** after the depth clear (fill `0xFFFC` on colour image `0x003DA800`), a sky-colour fill
  from the horizon down, then the panorama as 2D texture rectangles. Details:
  - 32×32 I8 tiles, dsdx 0.5 / dtdy 0.666, so 64 px by 48 lines;
  - images on the heap 0x400 apart, a grid of 10 columns by 4 rows in Greece (rows start `0x802CA8D0`,
    `0x802CD0D0`, `0x802CF8D0`, `0x802D20D0`);
  - the yaw sets the first column's x and s, the pitch sets the top row's y and t, and looking up draws
    more rows down the screen;
  - each tile is 11 commands: SETTIMG, tile setup and LOADBLOCK, then TEXRECT with its two half-commands.

  Emitting function not identified (`BH_DL_CENSUS`, docs/findings/phase-07.md change 4).
- **Player model:** `func_800EF14C_FE0FC` (outside overlay).
  - Before the call it loads a base scale, multiplies position and heading (`D_80052B34` instance) and
    multiplies the root pose, then sets segment 7 to fifteen bone matrices from `func_8000CC3C_D83C`
    and calls `0x010031E0` (Black Adam `0x050408F0`).
  - Inside the model: the torso list under those matrices, then per part `G_MTX` modelview|mul|push of
    `0x07000000 + 0x40·n`, the part list, and `G_POPMTX` back up the chain.
- **Player position:** `D_80052B34` points at his `VehicleInstance`, x at +0 and z at +4 (s16), also
  while on foot. By the time a frame's list is submitted the next frame's movement has already been
  applied (walking: 10–14 units ahead of what the list drew).
- **Shadows:** `func_800E988C_F883C` per vehicle/character. The shadow is a five-vertex quad (corners
  ± rotated half-extents, centre last, heights from the terrain), built in world coordinates in the
  frame's vertex buffer, drawn with `gSPVertex(5)` and two `gSP2Triangles` under the shared world
  matrix. Texture `0x05000000` when on foot.
- **Aiming reticle:** `func_800A2D98_B1D48` stores the aim point in `D_8014F618` (+0..+8), the billboard
  axes scaled by camera distance, and emits it through `func_800A2260_B1210`. It is texture
  `0x01009A70` (I4) and a nine-vertex world-space billboard (centre is vertex 4) under the world matrix
  `0x80031160`, followed by a model `0x0100ACB0` at the target. `func_800A2B58_B1B08` redraws the
  billboard fainter.
- **Display-list buffers:** two, alternated per frame (`D_801CE710 - D_80031B84 * 0x22B00`); top-level
  list at `+0x280` (`0x801CE990`).

- Two microcodes per frame: F3DEX 1.21 for the scene, L3DEX 1.21 used by some lists (RT64 must know
  it; PORTING.md "Renderer").
- Intro story text is drawn as 32×32 textured rectangles (`tex:0x0409xxxx`, F1 panel).

## Audio

- Output 32000 Hz, ~800 frames per buffer at 60 buffers/s (`BH_AUDIO_STATS=1`).
- Audio thread `func_80000730_1330` (thread id 5), woken by the scheduler's retrace message; each
  retrace `func_8000091C_151C` builds one task. Frame length:
  `outLen = (s16)((D_800431A8 − (osAiGetLength() >> 2) + 0xB0) & 0xFFF0)`, raised to `D_800431A4` if
  below it (signed). Command lists alternate between two `0x8000`-byte buffers (`D_8003FB20[0..1]`);
  the task's `data_size` is the length `alAudioFrame` returned. Read from decomp `core/1050.c` (a
  matching function). A large `osAiGetLength` wraps `outLen` positive and overruns the buffer —
  PORTING.md, Audio.

## Input

- One controller; the game reads it continuously from its own thread (Threads above).
- **Rumble:** strength by pulse density. `func_80001190_1D90`, once per controller-loop pass in gameplay
  modes 1, 3 and 0xB:
  - adds `(D_80047680 >> 4)³ / 512` to `D_8004767C`;
  - at ≥ 0x100 queues start (`func_80001050`) and subtracts 0x100; otherwise it queues stop (`func_8000108C`);
  - intensity `D_80047680` falls by `D_80047684` per pass after a hold of `D_80047688` passes;
  - past 10,001 passes the output fades with `(pass count)^-3`;
  - with the pak lost, it re-checks every 2,500 passes.

  The rumble thread `func_80000ED4_1AD4` (pri 15) turns the queue into `osMotorStart/Stop/Init`. In the
  port the loop makes ~25,000 passes a second (`BH_RUMBLE_TRACE=1`), so pass-counted timings run fast
  (phase-05.md).

## Saves

- 4 Kbit EEPROM; the slot record is read with `osEepromLongRead(…, 0, buf, 0x1BD)` (56 blocks).
  Written during name entry (`saves/bh.us.0.bin`, 512 bytes).

---

## Keeping this current

This file describes the *game*. When a later change discovers a new address, table, format or
drawing convention -- or corrects one written here -- update this file in the same commit. Facts
about N64Recomp, librecomp, ultramodern or RT64 belong in [PORTING.md](PORTING.md).
