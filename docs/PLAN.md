# Build plan

The plan this project is executed against. It is written before the work and kept as written, so
the reasoning behind each phase survives. Where the work overturns something here, add a note
under *Departures* rather than rewriting the text. What was found goes in [findings/](findings); the
reference someone else can use goes in [PORTING.md](PORTING.md) (port facts) and
[GAME-INTERNALS.md](GAME-INTERNALS.md) (game facts).

**Status (2026-09-15):** 00-04 done (boots to Greece gameplay); 05, 06, 07, 08 done as far as
measurable without a person — each awaits Daniel's playtest items (listed in its findings); 09 (Linux,
docs, packaging) gate met: clean clones build on both platforms, packages hold no game data
([findings/phase-09.md](findings/phase-09.md)); the release waits for Daniel. Since then Daniel's
playtest fixes are checkpointed below (sky, player model and shadow, rumble, reticle, map crash, HUD
anchors). Repository created at Daniel's request on 2026-09-15: github.com/danielgomesvieira2000/body-harvest-recomp (public, D10); no release yet. The session runs **autonomously** (Daniel: `/new-port
Body_Harvest rom.z64 autonomous`): at each choice the recommended option is taken and logged under
*Decisions*.

> **Departures from this plan** (added as they happen):
>
> - *Phases 07 and 08 before 06's gate*: their measurements need no one; the HUD classes and the
>   motion artefacts need Daniel at the F1 panel and in play, asked for in the same playtest.
> - *Phase 05 → 06 order*: phase 05's exit is a playtest by Daniel (rumble, pitch, a building, a level
>   change). Everything measurable without him is done ([findings/phase-05.md](findings/phase-05.md)).
>   Phase 06 starts before that gate so the playtest uses the real launcher and settings; the phase 05
>   checks are asked for together with phase 06's. (Autonomous session, 2026-09-15; Hybrid Heaven took
>   the same departure.)

## Goal

A native PC port of **Body Harvest** for Windows and Linux, built with N64Recomp on
N64ModernRuntime and RT64, with the RecompFrontend launcher and menus, sharing the harness,
frontend and dev tooling of Wave Race 64: Recompiled (the series reference build).

Enhancements for the first release:

- **Widescreen:** a genuinely wider view with no edge culling visible (the game culls entities and
  terrain on the CPU against 4:3), HUD anchored to the frame edges, no black bars in any aspect mode.
- **High frame rate:** matrix interpolation in RT64; the game keeps its own update rate.

## Target

| | |
|---|---|
| Game | Body Harvest (USA), game code `NBHE` |
| SHA-1 | `bbb6666f5014a473747ee4145f036d9fb25d7348` |
| XXH3-64 | `0x9949AA9BC66F33CB` |
| Header CRC | `0x5326696F 0xFE9A99C3` |
| Entry point | `0x80000400` |
| Save type | EEPROM (4 Kbit, inferred); Rumble Pak |
| Players | 1 |

One revision only. Every address in the project is tied to it.

## The decision this project is built on

A decompilation exists that builds this exact dump byte-for-byte (jaytheham/body-harvest-decompilation,
~1,000 functions still `GLOBAL_ASM`, the ROM matches regardless). So the input is **route A**: the
decomp's own matching build produces `build/bh.us.elf`, N64Recomp reads it in ELF mode, and
enhancements are written against its names. The decomp is never modified; it is pinned as a
submodule and built in a WSL mirror, and the port post-processes the ELF (sizes for hand-written
asm, ABS assignments) in its own scripts (playbook 02).

The decomp publishes **no licence**: it is used as a build input and a source of names. No file from
it is copied into this repository. MIPS C patches (`RECOMP_PATCH`) would need its headers, so the
port prefers TOML hooks, instruction patches and native C++ replacements; if MIPS patches become
necessary, they include the headers from the submodule at build time instead of copying them.

## What is already known about the game

Measured in [findings/phase-00.md](findings/phase-00.md):

- Resident `core` at `0x80000400` (ROM `0x1000`, `0x3F720` bytes).
- Eight **uncompressed** overlays sharing two vrams: gameplay `frontend` / `outside` / `inside` at
  `0x80070270`, levels `greece` / `java` / `america` / `siberia` / `comet` at `0x802D4CD0`.
- One loader: `func_800101F0_10DF0(dest, rom, size)` → chunked `osPiStartDma` (`0x800` pieces).
- 3,049 FUNC symbols; 181 zero-size, 6 ABS (fixed in phase 01).
- F3DEX 1.21 + L3DEX 1.21 (RT64 maps L3DEX 1.xx to no GBI — a third-party port hit this); stock
  `aspMain` at ROM `0x2FF10`.
- EEPROM saves; `osMotor*` Rumble Pak; no Controller Pak saves; no `osMemSize` reference.
- An existing PC port (12feihu/BodyHarvest-PC-Port, MIT) documents several traps; its findings are
  re-verified here before being relied on and credited in README.

## Decisions

| # | Decision | Options considered | Recommendation | Decided by |
|---|---|---|---|---|
| D1 | ROM revision | USA `NBHE` / PAL | **USA**: the revision the decomp matches (series revision rule) | series rule |
| D2 | Fork or fresh | fork 12feihu/BodyHarvest-PC-Port (BAR precedent) / fresh port | **fresh**, modelled on Wave Race; the existing port is read for findings and credited. It has no RecompFrontend, no Linux, hand edits in generated code and in RT64, and its README calls itself a messy starting point — the "hard plumbing" worth keeping from it is a handful of facts, not code | recommended option, autonomous session (2026-09-15) |
| D3 | Symbol input | decomp matching build ELF (route A) / decomp splat config asm-only (route B) / splat from scratch | **route A**: the full ROM matches today; the ELF carries real names and sizes for compiled C | playbook 01, autonomous |
| D4 | Decomp consumption | submodule + build / vendor / names only | **submodule `lib/bh-decomp` pinned at `46006777`**, built in a WSL mirror (`~/.cache/body-harvest-recomp/decomp`), nothing copied into the tree (no licence) | recommended option, autonomous |
| D5 | Runtime / renderer / frontend pins | Wave Race pins (N64ModernRuntime `cdf5abb` upstream, RT64 `5473732`, RecompFrontend `b1a1477`) / Daniel's `controller-pak` fork `b0b2b6e` (RAY2, HH) | **Wave Race pins**: the fork is the series rule for Controller Pak games and this game saves to EEPROM. Switch to the fork only if a bug it fixes is measured here (record it) | series rule, autonomous |
| D6 | Overlay dispatch | WR64: `use_lookup_for_all_function_calls` + runtime function table + resident registration + announce on the loader / resolve statically | **WR64 mechanism**, announcement on `func_8000FFC0_10BC0` (whole transfer) rather than `osPiStartDma` (chunks) | playbook 04, autonomous |
| D7 | Identity | — | display "Body Harvest: Recompiled"; slug / repo / exe `body-harvest-recomp`; env prefix `BH_`; CMake `BH_WITH_*`; settings `%LOCALAPPDATA%\body-harvest-recomp` (Linux `$XDG_DATA_HOME/body-harvest-recomp`) | series rule |
| D8 | Player count | 1 | **1**: `set_player_count_range(1,1)` + `set_single_player_mode(true)` (RAY2 → PW64 shape) | series rule |
| D9 | Frontend and tooling source | copy WR64 / copy HH (a WR64 copy with later fixes) | **WR64 literally**, with fixes that landed later in HH/PW64 (portable-mode hygiene, test sandbox) taken where WR64 lacks them, each noted in PORTING.md | series rule |
| D10 | Public GitHub repo | create now / wait until asked | **not created**: the request did not ask for a repo ("Create the GitHub repo only if Daniel asked"). Everything is committed locally on `main` | skill rule. **Update 2026-09-15:** Daniel asked ("lets push to github in a new repo called body-harvest-recomp"); created public like the other ports, `main` pushed |

## Phases

Each phase is entered only through the previous phase's gate. Every phase ends with the tree
building; from phase 03 on, with the game run and looked at.

### 00 — Toolchain, survey and skeleton
**Entry:** a legal dump and this plan.
Repository and submodules (N64ModernRuntime, RT64, RecompFrontend at D5 pins; `lib/bh-decomp`),
`.gitignore` that refuses game data, phase-gated CMake (`BH_WITH_RUNTIME/RECOMPILED/FRONTEND`),
`tools/check_toolchain.ps1`, `tools/identify_rom.py`, survey.
**Gate:** the tree configures and builds; `--identify` accepts the dump and rejects others; the
survey is written to `findings/phase-00.md`; every stack-patch anchor verified against the pins.

### 01 — ROM to ELF
**Entry:** phase 00 gate.
`tools/wsl_build_elf.sh`: mirror the pinned decomp to WSL, `make extract && make`, refuse unless
`build/bh.us.z64: OK`; then `tools/fix_elf.py`: give zero-size FUNC symbols their extent, resolve
ABS FUNC symbols, report counts; `tools/verify_elf.py`: every code section byte-identical to the dump,
every address-encoding symbol placed at its address, zero `FUNC ABS`, zero size-0 `FUNC`.
**Gate:** the verify output is all zeros and recorded in `findings/phase-01.md`.

### 02 — First recompile
**Entry:** phase 01 gate.
`recomp/body-harvest.us.toml` (lookup for all calls; `ignored`/`stubs` justified per entry),
`recomp/aspMain.us.toml` (measured extent, table read from ROM), N64Recomp + RSPRecomp under WSL,
`gen_reimplemented_decls.py`, `gen_runtime_func_table.py`, stage counts.
**Gate:** all of `RecompiledFuncs/` compiles into a static library, with function counts matching
the ELF at every stage.

### 03 — Runtime harness
**Entry:** phase 02 gate.
Wave Race's harness renamed: `main.cpp` (GameEntry, config path, `--identify`, `--version`),
callbacks, renderer, crash handler, lookup-miss report, sections/overlays registration, libultra
stubs for what nobody implements, submodule patch scripts (`tools/patch_all.py`).
**Gate:** `on_init` and the first thread-create print, 10 of 10 runs.

### 04 — Boot bring-up
**Entry:** phase 03 gate.
Overlay announcements, L3DEX GBI mapping (scripted RT64 patch), spin loops (`find_spin_loops.py`,
thread sampler), hand-coded hardware leaves (`osAiGetLength`, `__osPiGetStatus`).
**Gate:** logos, title and attract render recognisably; a new game reaches Greece gameplay.

### 05 — Graphics, audio and saves correctness
**Entry:** phase 04 gate.
Audio through the recompiled `aspMain` at correct pitch (WR64 resampler, queue headroom); EEPROM
save survives a restart (the `0x1BD`-byte long read); Rumble Pak detected and rumbling; the inside
(building) overlay and a level change (Greece → Java) work.
**Gate:** Greece from the start through the first building and a save/load, correct visuals and audio,
then confirmed by Daniel in play.

### 06 — Frontend
**Entry:** phase 05 gate.
WR64 frontend copied: launcher with ROM picker and hash verification, Graphics/Sound/Controls/Mods
tabs, default keyboard layout, single-player input merge, Select opens the menu, rumble strength,
main volume, F1 debug menu + HUD inspector (fed by this port's list walker).
**Gate:** a stranger picks their dump in the launcher and plays with a pad, and with the keyboard and
no pad attached.

### 07 — Widescreen
**Entry:** phase 06 gate.
Measure first: drawn region/scissor, frustum, far plane, CPU culls (RT64 free camera). Then: widen the
game's cull (entity bbox, terrain LOS, whatever the census shows) with the matrix left to RT64 Expand,
HUD anchoring through the inspector.
**Gate:** every screen right at 16:9, 21:9 and in a 4:3 window: nothing cut off, doubled, stretched,
popping at the edges, or black-barred.

### 08 — High frame rate
**Entry:** phase 07 gate.
Measure the game's rate per mode; presentation mode that allows interpolation; pairing log; ids only
where a measurement demands; 2D not interpolated.
**Gate:** smooth motion at a raised rate with no interpolation artefacts in outside gameplay, a
building interior and a cutscene; game speed unchanged.

### 09 — Linux, packaging, first release
**Entry:** phase 08 gate (or Daniel's go).
WSL Linux build, `tools/package_release.*`, README/BUILDING/CHANGELOG/THIRD_PARTY_NOTICES. Repo
creation and release only when Daniel asks.
**Gate:** a clean clone builds with the documented commands on both platforms; packages contain no
game data.

## Testing throughout

- Build after every change; never leave the tree not building.
- From phase 03, run the game after every behavioural change and look at it: scripted input
  (`BH_INPUT_SCRIPT`), `tools/capture_frames.py`, the log (`<settings>\bh.log`).
- Test runs use `tools/test_sandbox.py` or clean up every file they create in the settings folder.
- A regression found is written in the findings before it is fixed.

## What would make this project stop

- The decomp's matching build stops being reproducible at the pinned commit and route B also fails.
- RT64's handling of F3DEX 1.21 / L3DEX 1.21 needs changes large enough to be a renderer project.

## Standing constraints

- No ROM, asset, or ROM-derived file is ever committed (ELF, asm, RecompiledFuncs, dumps, saves).
- Nothing from the unlicensed decomp is copied into the tree.
- Generated code is never hand-edited. Fix the config, a patch, or a script in `tools/`.
- Submodules are never hand-edited: every change is an idempotent `tools/patch_*.py`.
- The frontend is consumed, not forked. The launcher owns Start Game; auto-start only via `BH_AUTOSTART=1`.
- No black bars in any aspect mode. Widescreen = a wider view. High frame rate = interpolation, never
  game-logic changes.
- F1 debug menu and HUD inspector in every build, live.
- Windows and Linux.
- Build RelWithDebInfo; a Debug build breaks audio timing.
- Docs in the same commit as the change: game facts in GAME-INTERNALS.md, port facts in PORTING.md.
  Findings stay as written, negative results included, inference marked.
- Measurements beat recollection: every claim in `docs/` is re-derivable from a tool or a named switch.

## Verified checkpoints

| Date | Commit | Submodule pins | What Daniel confirmed |
|---|---|---|---|
| 2026-09-15 | `443a435` | N64ModernRuntime `cdf5abb`, RT64 `5473732`, RecompFrontend `b1a1477`, bh-decomp `4600677` | Outdoor sky fills a 16:9 window, aiming at the sky included. Open: player model jitters under interpolation |
| 2026-09-15 | `61e414d` | same | Player model and his shadow steady under interpolation (on/off runs watched) |
| 2026-09-15 | `52a12ed` | same | Rumble felt and correct |
| 2026-09-15 | `b21787b` | same | Aiming reticle steady while aiming |
| 2026-09-15 | `2f7efea` | same | Pause map opens without crashing |
| 2026-09-15 | `da84d95` | same + `patch_runtime_timer.py` | Rumble timing correct with the controller loop at ~285 passes/s |
| 2026-09-15 | `dfdf975` | same | Entering a house, walking inside and leaving works |
| 2026-09-15 | `d7d3049` | same | Radar right, health/alien bars and weapon panel left at 16:9; Linux build runs to gameplay and the pause map (WSLg) |
