# Porting reference

How this port is put together and why each piece is the way it is: the pipeline from a dump to an
executable, the runtime harness, patches, and the enhancements. Facts about the game are in
[GAME-INTERNALS.md](GAME-INTERNALS.md); how each fact was found is in [findings/](findings).

Each section states the **symptom first**, because a symptom is what you will have when you come
looking.

Pinned upstream revisions:

| Submodule | Commit |
|---|---|
| N64ModernRuntime | `cdf5abb` (upstream; Wave Race 64's pin) |
| N64Recomp (inside N64ModernRuntime) | `81213c1` |
| RT64 | `5473732` |
| RecompFrontend | `b1a1477` |
| bh-decomp (jaytheham/body-harvest-decompilation, no licence) | `4600677` |

## Contents

1. [The pipeline](#the-pipeline)
2. [The ELF](#the-elf)
3. [Recompiling](#recompiling)
4. [The harness](#the-harness)
5. [libultra the runtime owns, and what it forgot](#libultra-the-runtime-owns-and-what-it-forgot)
6. [Audio](#audio)
7. [Overlays](#overlays)
8. [Renderer](#renderer)
9. [Frontend and the F1 inspector](#frontend-and-the-f1-inspector)
10. [Submodule patches](#submodule-patches)
11. [Testing and diagnostics](#testing-and-diagnostics)

## The pipeline

```
rom.z64 ─▶ lib/bh-decomp (WSL mirror, make) ─▶ build/bh.us.elf ─fix_elf.py─▶ elf/bh.us.fixed.elf
        ─verify_elf.py (all zero)─▶ N64Recomp ─▶ RecompiledFuncs/ ─CMake─▶ body-harvest-recomp
rom.z64 ─RSPRecomp (aspMain)──────────────────▶ RecompiledFuncs/aspMain_rsp.cpp
```

One command: `wsl -d Ubuntu -e bash tools/regenerate.sh` (builds the recompiler if needed, then
`wsl_build_elf.sh`, then `recompile.sh`). Every stage checks its end state and prints counts.

## The ELF

**Route A** (playbook 01): the decomp's own matching build. `tools/wsl_build_elf.sh` rsyncs the
submodule to `~/.cache/body-harvest-recomp/decomp` (a build on `/mnt/c` is slow), copies the dump in as
`baserom.us.z64`, runs `make extract && make`, and refuses the ELF unless the log says
`build/bh.us.z64: OK`. The decomp's `requirements.txt` pins `tree-sitter` versions with no wheel for
current Python; they are for m2c/permuter only and are skipped.

**Symptom: functions silently not recompiled / "Failed to find function".** `tools/fix_elf.py` repairs
three symbol-table traps before N64Recomp sees the ELF:

| Trap | In this ELF | Repair |
|---|---|---|
| Zero-size FUNC (hand-written asm `glabel` has no `.size`) | 173 | size = next FUNC/OBJECT start in the section |
| FUNC in `SHN_ABS` (linker assignments from `undefined_syms.us.txt`) | 6 | demote aliases with a section twin to NOTYPE; rebind the 2 whose assignment shadows the only definition (`func_8011D260_12C210`, `func_80128504_1374B4`) by the ROM offset in their name |
| Aliases at one address | `_bcopy/bcopy`, `_bzero/_blkclr/bzero/blkclr` | keep one FUNC |
| Code no symbol covers (libultra `static` functions from the decomp's archive) | 4 functions in 3 gaps in core | rebind the referenced ABS `func_XXXXXXXX` name as a FUNC sized to the next boundary (keeping `jr $ra`'s delay slot) |

The last one was found as a run-time miss at `0x8001F8B0` from `alBnkfNew` (phase 04).

**Symptom: the game crashes in RT64 `loadTileOperation` when the pause map opens.** The decomp names
the game's set-screen-size routine (`0x8000E4B0`, two stores) `osSetTime`, a name on N64Recomp's
runtime list. So its calls went to ultramodern's `osSetTime`, which set the OS clock instead. The size
(`D_8005BAEC/F0`) stayed 0, and the pause transition's framebuffer tiles arrived as
`LOADTILE … lrs 0 lrt 0`; RT64's unsigned row count underflowed. `fix_elf.py` renames it `bhSetSize`
(table `MISNAMED`, checked by address and size). Audit: of the 125 runtime-owned names in the ELF, it
was the only one outside the libultra address range.

`tools/verify_elf.py` then checks: every PROGBITS section byte-identical to the dump at the ROM address
N64Recomp computes (PT_LOAD paddr + offset), yaml code segments agree, address-named functions placed
exactly, no ABS / size-0 / overlapping FUNC. Data labels whose names disagree with their address (31, a
decomp naming slip) are listed, not failed.

## Recompiling

`recomp/body-harvest.us.toml`: ELF mode, entry `0x80000400`, `use_lookup_for_all_function_calls = true`
(overlays share vrams), `relocatable_sections_path = overlays.txt` (the eight overlays; the ELF has no
`.rel` sections and N64Recomp accepts that).

| Entry | Why |
|---|---|
| `ignored`: `func_800235E4_241E4` | Tail of `__osException` split off by splat; branches back into it. The runtime owns exceptions |

`recomp/aspMain.us.toml`: stock `aspMain` (byte-identical to Pilotwings 64's), ROM `0x2FF10`, size
`0xE20` (the task's declared `0xF80` overruns into the next microcode), IMEM `0x04001080`, 16-entry
table from ROM `0x3FC70`.

Counts (`tools/count_recompiled.py`): 3,045 FUNC − 124 runtime-owned − 1 ignored = 2,920 emitted.

## The harness

Copied from Hybrid Heaven (itself Wave Race 64's), renamed; Controller Pak and Konami file-table code
left out. Differences that matter:

| Where | What |
|---|---|
| `src/main.cpp` | `SaveType::Eep4k`; `register_overlays()` before start; `set_message_queue_control` defaults |
| `src/callbacks.cpp` | one player; port 1 reports `Pak::RumblePak` (`BH_NO_RUMBLE_PAK=1`: none); `aspMain` at `0x8002F310`; audio command-list copy at scratch `0x807F0000` |
| `src/overlays.cpp` | section tables, runtime function table, resident registration, loader wrapper |
| `src/libultra_glue.cpp` | wrappers for runtime libultra that lost a side effect (next section) |
| `src/calltrace.cpp` | `BH_TRACE_FUNCS` call tracer |

**Scratch RDRAM** (upper 4 MB, *inferred* free — no `osMemSize` use, game data ends below `0x80400000`):

| Range | Use |
|---|---|
| `0x80700000`–`0x80780000` | HUD rewriter copies (two 256 KB buffers) |
| `0x80780000`–`0x80780040` | identity matrix the rewriter's model and shadow groups multiply by |
| `0x807EF000`–`0x807EF064` | controller-latency timer + queue |
| `0x807F0000`–`0x80800000` | audio command-list private copy |

## libultra the runtime owns, and what it forgot

**Symptom: two display lists, then a frozen black window; audio and VI keep running.** Two causes, both
fixed in `src/libultra_glue.cpp`:

1. **`__osEepromTimerQ` never created.** The game's hand-written `osEepromLongRead` (`func_8001D5A0`)
   waits ~16 ms per 8-byte block on a timer whose message goes to `__osEepromTimerQ` (`0x8006CA28`).
   libultra's `osContInit` creates that queue (cartridge instruction `0x8001CEDC`); the runtime's
   `osContInit` does not. Wrapper at `0x8001CD00`: runtime `osContInit`, then
   `osCreateMesgQueue(0x8006CA28, 0x8006CA40, 1)`.
2. **A controller thread that never blocks.** `func_80002EF8_3AF8` (priority 5) loops
   `osContStartReadData` → `osRecvMesg(SI)` → process. The runtime completes SI transfers on the spot, so
   the receive never blocks and ultramodern's cooperative scheduler never runs the priority-4 game
   thread. Wrapper at `0x8001D6E0`: after starting the read, block the thread on a timer for
   `BH_SI_LATENCY_MS` (default 1; 0 = off).

General lesson: a libultra function the runtime owns by name loses every side effect the cartridge's
body had; an unnamed routine that depended on it hangs far from the cause.

## Audio

`src/callbacks.cpp` (Wave Race's design): recompiled `aspMain` run from a private copy of the command
list, non-`Broke` exits drop one frame, the port resamples and keeps the SDL queue ~30 ms deep by
under-reporting `osAiGetLength`.

**Symptom: "task failed with UnhandledJumpTarget: 59389 commands", then a crash in
`func_8000091C_151C` (`funcs_13.c:2386`).** Seen first on a slow WSLg run; reproduced on Windows in
seconds with `SDL_AUDIODRIVER=disk SDL_DISKAUDIODELAY=1000` (a device that drains 512 frames a second).
The game sizes each audio frame as `(s16)((nominal − osAiGetLength()/4 + 0xB0) & 0xFFF0)`, raised to a
minimum by a signed compare (GAME-INTERNALS.md, Audio). When the host queue holds more than ~33,000
frames, the s16 wraps positive, `alAudioFrame` writes tens of thousands of commands into its
`0x8000`-byte buffer and the heap after it is overwritten. Hardware never reports more than one buffer.
Fixed in the port, not the game:

- `get_frames_remaining` never reports more than 4,096 frames (the subtraction then goes negative and
  the game uses its minimum);
- `queue_samples` drops buffers while the device already holds over a second (`[bh] audio device is not
  draining: N buffers dropped`);
- a failed task longer than 4,096 commands is not bisected (the prefix re-runs are quadratic).

Same run after the fix: Greece gameplay at 20 frames/s, no failed task. A normal run
(`BH_AUDIO_STATS=1`, 75 s) is unchanged: 35 of 36 windows with nothing zero-filled.

## Rumble

**Symptom: no rumble felt, although the game calls `osMotorStart`.** The game sets strength by pulse
density: every controller-loop pass it starts or stops the motor from a sigma-delta accumulator
(GAME-INTERNALS.md, Input). recompinput samples an on/off flag once per frame, which misses the pulses.
`src/callbacks.cpp` instead measures the time-weighted on-fraction between frames. It low-passes that
like a motor, multiplies by the Rumble Strength slider and drives both pad motors with
`SDL_GameControllerRumble`; recompinput's `update_rumble` is not called. `BH_RUMBLE_RAW=1` restores the
old path.

## Overlays

**Symptom: lookup misses inside `0x80070270`–`0x80149380` or `0x802D4CD0`–.** The overlay at that window
was not announced.

- `register_runtime_functions()` (from `on_init`) first unloads all eight overlays: librecomp's boot load
  (ROM `0x1000` + 1 MB) registers the frontend overlay at `0x8003FB20` and records it as loaded there,
  which would offset its first real load.
- Resident functions (sections whose address nobody shares: core) are registered up front; runtime
  libultra at cartridge addresses; the loader wrapper last.
- `func_8000FFC0_10BC0(queue, dest, rom, size)` is wrapped: a load whose ROM start equals an overlay's
  evicts every loaded overlay overlapping its range by id, loads it by id, then runs the game's copy.
  Not `osPiStartDma` (Wave Race 64's hook): this loader DMAs in `0x800` pieces.
- `BH_DEBUG_LOADS=1` prints every load (`[bh-load] overlay ...` / `data ...`).

## Renderer

**Symptom: Gfx-thread crash in `RT64::RDP::loadTLUTOperation` reading a wild address** (after START on
the intro). RT64 `5473732` registers L3DEX 1.x as `GBIUCode::Unknown`, so an L3DEX list has only RDP
handlers and is walked past its end. `tools/patch_rt64_l3dex.py` routes the four L3DEX 1.x entries to
`GBIUCode::F3DEX` (finding from 12feihu's BodyHarvest-PC-Port). L3DEX's line command (0xB5) then decodes
as F3DEX's quad.

Presentation mode: `PresentEarly` (Wave Race's; `BH_PRESENT_MODE=console|skip|early`).

**Symptom: black strips at the right and bottom of gameplay, in any window.** The game renders gameplay
into 304×230 and relies on the VI's X/Y scale to stretch it; ultramodern ignores `osViSetXScale/YScale`
and RT64 presents without VI scale. `src/widescreen.cpp` replaces `setGameplayResolution` (`0x80006D84`)
with the game's own `setFullResolution`, so gameplay draws 320×240 (`BH_FULL_FRAME=0`: off).

**Symptom: title and menu backdrops pillarboxed at 16:9.** They are grids of 32×32 texture rectangles;
`src/dlcensus.cpp` detects a full row of them per frame and gives the tiles `stretch` through the
inspector's per-frame class layer (`BH_NO_BG_STRETCH=1`: off).

**Symptom: the sky covers only the middle 4:3 of a wide window** (black or a flat block beside it). The
panorama is rows of 2D texture tiles on heap images that change with the camera (GAME-INTERNALS.md),
so no fixed identity can be tagged. `src/dlcensus.cpp` finds every row of rectangles that shares top and
bottom edges and joins end to end from x 0 to 319. It gives all of them `stretch`, testing the rectangles
one by one, not the panel's merged elements. The first version stopped at the first row, so looking up
left the lower rows at 4:3. `BH_NO_SKY_STRETCH=1`: off.

**Symptom: terrain missing at the sides of a wide picture.** The game culls terrain tiles and entities
against `D_8014FD2A` (full horizontal cull angle, BAM). `src/widescreen.cpp` wraps its setter
`func_800B33BC_C236C` and stores the angle widened to the window aspect ×1.1. The setter is in the outside
overlay: wrappers for overlay functions are re-registered from `bh::overlay_loaded` after every load of
that overlay, because the load overwrites the function-map entries.

## Frontend and the F1 inspector

RecompFrontend as Wave Race 64 / Hybrid Heaven: launcher (Load ROM → Start Game, Controls, Settings,
Mods, Quit), General (rumble strength) / Graphics / Sound (Main Volume, Mute When Not In Focus) /
Controls / Mods tabs, series keyboard layout, single-player mode (keyboard merged with every pad),
Escape / pad Back toggle the menu, first run fullscreen (not while `BH_INPUT_SCRIPT` is set), bindings
written on first run and exit, `BH_AUTOSTART=1`. Launcher art: `tools/make_logo.py` (original).

**F1 = RT64 developer UI + "Body Harvest HUD" panel**, in every build. Unlike Hybrid Heaven (F3DEX2),
this game's lists are F3DEX 1.x, so the feed and rewriter were rewritten against RT64's own decoders
(`include/bh/gbi_f3dex.h`):

| F3DEX 1.x layout the walker depends on | |
|---|---|
| `G_MTX` 0x01 | params bits 16-23: PROJECTION 0x01, LOAD 0x02, PUSH 0x04 |
| `G_MOVEMEM` 0x03 | type bits 16-23; viewport 0x80 |
| `G_VTX` 0x04 | count bits 10-15, first index bits 17-23 |
| `G_DL` 0x06 | bit 16 = branch |
| `G_TRI1` 0xBF / `G_TRI2` 0xB1 / `G_QUAD` 0xB5 | indices ×2 at bits 17/9/1 (and 25 for the quad) |
| `G_ENDDL` 0xB8, `G_POPMTX` 0xBD (`w1 == 0` pops one), `G_MOVEWORD` 0xBC (type bits 0-7, segment bits 10-13) | |
| `G_TEXRECT` 0xE4/0xE5 | RT64 HLE consumes the next two commands unconditionally |
| extended GBI hook | `G_SPNOOP` 0x00 (define nothing; `F3DEX_GBI_2` would select 0xE0) |

`src/dlcensus.cpp` publishes each frame's 2D elements (identities per `include/bh/hudid.h`);
`src/hudrewrite.cpp` applies classes live; `hud.json` in the settings folder; promote with
`tools/promote_hud_tags.py`. Built-in tags: weapon icon, ammo box and digits `left`.

**Symptom: the radar and the health/alien bars stay in the 4:3 middle, and the panel cannot move them.**
- They are top-level 2D triangles, which a class per identity did not reach.
- The bar frames share textures with the vehicle's bars on the right.

The game's own draw functions are wrapped instead (`src/widescreen.cpp`: bars by their side argument,
radar right, weapon panel left). Each writes a `G_NOOP` anchor marker (`include/bh/hudrewrite.h`) before
and after its drawing. The rewriter aligns the viewport and rects to that edge for everything between
the markers, and a tagged identity still wins. `BH_NO_HUD_ANCHORS=1`: off.

## Submodule patches

Every change to a submodule is an idempotent script in `tools/`, run by `python tools/patch_all.py`.
Rerun it after any submodule update.

| Script | Submodule | What and why |
|---|---|---|
| `patch_rsprecomp.py` | N64Recomp | RSP indirect jumps ignore the low two bits (Wave Race 64) |
| `patch_n64recomp.py` | N64Recomp | `use_lookup_for_all_function_calls` config key (Wave Race 64) |
| `patch_librecomp.py` | librecomp | failed lookup reports caller and thread (`bh_report_lookup_miss`) |
| `patch_runtime_shutdown.py` + `patches/runtime-shutdown.patch` | librecomp, ultramodern | join workers before freeing RDRAM (Wave Race 64's diff for upstream `cdf5abb`; HH's diff is for the fork) |
| `patch_recompinput.py` | recompinput | auto-assign controllers |
| `patch_rt64_eventfilter.py` | RT64 | remove the SDL event filter on destruction |
| `patch_rt64_inspector.py` | RT64 | port hook in the F1 UI; F2 unbound |
| `patch_rt64_texturepacks.py` | RT64 | texture packs from mods |
| `patch_rt64_pairing.py` | RT64 | interpolation pairing counters |
| `patch_rt64_l3dex.py` | RT64 | L3DEX 1.x gets F3DEX's command set |

## Testing and diagnostics

### Environment variables

| Variable | Effect |
|---|---|
| `BH_INPUT_SCRIPT=<file>` | timed input script (`tools/scripts/start-spam.txt`) |
| `BH_AUTOSTART=1` | start the stored dump without the launcher |
| `BH_DEBUG_LOADS=1` | log overlay and data loads |
| `BH_TRACE_FUNCS=0xADDR,...` / `BH_TRACE_LIMIT=<n>` | print calls to resident/runtime functions with args and result |
| `BH_FRAME_STATS=1` | game frame rate (from `osViSwapBuffer`) vs presented rate, every 2 s |
| `BH_PAIRING=1` | RT64 transform-pairing counters every 2 s |
| `BH_SAMPLE=1` | thread sampler every 2 s (`tools/symbolize_log.py` resolves it) |
| `BH_AUDIO_STATS=1` / `BH_AUDIO_DUMP` / `BH_AUDIO_HEADROOM_MS` / `BH_AUDIO_PERIOD` / `BH_AUDIO_NO_RESAMPLE` | audio diagnostics and knobs |
| `BH_SI_LATENCY_MS=<n>` | controller transfer latency (default 1; 0 = immediate, which stalls boot) |
| `BH_NO_RUMBLE_PAK=1` | report an empty accessory slot |
| `BH_RUMBLE_RAW=1` / `BH_RUMBLE_TRACE=1` | rumble through recompinput's on/off model (felt as nothing) / log motor calls, duty and strength each second |
| `BH_PRESENT_MODE=console\|skip\|early` | RT64 presentation mode |
| `BH_DL_CENSUS=<n>` | census of every n-th display list (first 12 rectangles, with s,t, dsdx,dtdy and colour image) |
| `BH_INSPECTOR=0` | hide the HUD panel |
| `BH_HUD_ELEMENTS_LOG=1` / `BH_HUD_REWRITE_TRACE=1` / `BH_NO_HUD_REWRITE=1` | HUD feed/rewriter diagnostics and A/B |
| `BH_TEST_INSPECTOR=<s>` / `BH_TEST_OPEN_SETTINGS=<tab>@<s>` / `BH_TEST_HUD_OVERRIDE` | test hooks |
| `BH_WINDOW_SIZE=WxH` / `BH_YIELD_MS` / `BH_SKIP_DL` | window size, spin-yield wait, skip display lists |
| `BH_FULL_FRAME=0` | gameplay keeps the game's 304×230 region (VI-scaled on hardware) |
| `BH_NO_BG_STRETCH=1` | full-screen 32-px tile backdrops stay 4:3 |
| `BH_NO_SKY_STRETCH=1` | the outdoor sky's tile rows stay 4:3 |
| `BH_NO_MODEL_IDS=1` | the player model's parts are left to RT64's own pairing |
| `BH_NO_SHADOW_INTERP=1` / `BH_SHADOW_TRACE=1` | the player's shadow steps at the game's rate / how often it is found |
| `BH_NO_RETICLE_INTERP=1` | the aiming reticle steps at the game's rate |
| `BH_NO_HUD_ANCHORS=1` | radar, bars and weapon panel stay in the 4:3 middle |
| `BH_NO_WIDE_CULL=1` / `BH_CULL_MARGIN=<pct>` / `BH_CULL_TRACE=1` | keep the game's 4:3 cull angle / margin over the aspect (default 10) / log every change |

### Test runs

- `python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --rom rom.z64 --seconds 70 --grab 20,45 --out <dir> --seed tools/test-seeds/graphics.json --env BH_INPUT_SCRIPT=...`
  runs a throwaway copy (`portable.txt`), grabs the window, keeps changed settings, deletes the copy.
  Seed the windowed `graphics.json`, or a clean profile opens fullscreen.
- `tools/shoot_run.ps1`, `tools/boot_runs.ps1` run the real build and **write to the real settings
  folder**; clean up after them.
- `tools/capture_frames.py` saved 0 frames of this window on the development laptop. `test_sandbox.py
  --grab 10,20` (single frames) and `--burst SECONDS,COUNT` (~30–36 a second, for
  `tools/frame_motion.py`) use `tools/print_window.py` (PrintWindow with `PW_RENDERFULLCONTENT`), which
  reads the window's own contents even when covered. Crop to the window size: the bitmap is the
  DPI-virtualised client size.

### Crashes

Windows: `src/crash_handler.cpp` prints the fault, module, function and source line. Linux: the same file
installs a SIGSEGV/SIGBUS/SIGFPE/SIGILL handler that prints the accessed address, the executable's load
base and a raw backtrace; resolve frames with
`llvm-symbolizer-21 --obj=build-linux/body-harvest-recomp 0x<offset>` (the `+0x…` in each line).

On Linux the first SIGTERM did not end a gameplay run (a second one did): use `timeout -k 10 <s>` in
scripts.

### Frame interpolation

Gameplay runs 20 game frames/s, presented at 60, with about one unpaired transform per frame
(`BH_PAIRING=1`). `frame_motion.py` on a burst while walking: 90 % of pairs change under Framerate
Display vs 47 % under Original (docs/findings/phase-08.md). Port-side groups go only where an artefact
was seen, both in `src/hudrewrite.cpp`:

**Symptom: the player model jitters while the world glides.** Adam's parts are separate transforms (bone
matrices pushed from segment 7 inside `0x010031E0`), and RT64's signature/nearest pairing mixes them up.
Fix: the model call is inlined, and before each bone matrix goes a group with an id from its segmented
address, linear order and translation always interpolated. An identity multiply under its own id gives
the torso a transform. `BH_NO_MODEL_IDS=1`: off. A pairing counter does not show this defect.

**Symptom: the player's shadow jitters.** It is a five-vertex quad rebuilt in world coordinates under the
shared world matrix, so RT64 sees a still transform. Fix: identity multiplies wrap it in a transform of
its own, with vertex interpolation, linear order and a fixed id. It is found as a five-vertex load whose
fifth vertex is the corners' mean, within 96 units of the player. It is *near*, not *at*, his position:
by submission the game has moved him 10–14 units on. `BH_NO_SHADOW_INTERP=1`: off; `BH_SHADOW_TRACE=1`
reports how often it is found.

**Symptom: the aiming reticle jitters while aiming.** It is a nine-vertex world-space billboard under the
static world matrix, the shadow's case. The same group applies, found by `SETTIMG 0x01009A70` followed
by a nine-vertex load, one id per occurrence (reticle and its ghost copy), with no interpolation across a
jump of more than 2,000 units. `BH_NO_RETICLE_INTERP=1`: off.

---

## Keeping this current

This file describes the *port*. When a later change fixes something in the toolchain, runtime or
renderer -- or finds something here no longer true -- update this file in the same commit, with the
symptom. Facts about Body Harvest itself belong in [GAME-INTERNALS.md](GAME-INTERNALS.md).
