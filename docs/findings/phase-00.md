# Phase 00 findings — survey and skeleton

**Gate:** met. The tree configures and builds (`build/`, all phase options off); `--identify` accepts
the dump (exit 0) and rejects Hybrid Heaven's (exit 2, size/cartridge/CRC listed); the survey is below;
every submodule patch script applies to the pinned revisions and reports "already patched" on a rerun.

Reproduce with:

```
python tools/identify_rom.py rom.z64
python tools/survey_rom.py rom.z64
wsl -d Ubuntu -e bash tools/wsl_build_elf.sh          # decomp mirror build + ELF audit (phase 01)
```

## Answer

The only supported dump is **Body Harvest (USA)**, and it is exactly the revision a public
decompilation (jaytheham/body-harvest-decompilation) builds byte-for-byte, so the port takes
**route A** (the decomp's own matching build as the ELF). The code layout is Wave Race 64's shape:
a resident `core` segment plus **eight uncompressed overlays sharing two vrams**, loaded by one
game routine that DMAs ROM to RAM. Saves are EEPROM; the game drives a Rumble Pak; one player.
An existing third-party PC port exists and is used as a source of findings, not as a base.

## Measurements

### Identity

| What | How | Result |
|---|---|---|
| Title / game code | `identify_rom.py` (header) | `BODY HARVEST`, `NBHE`, version byte 0 (decomp calls it "B6.5") |
| Size | file | 12,582,912 bytes (12 MB) |
| SHA-1 | `identify_rom.py` | `bbb6666f5014a473747ee4145f036d9fb25d7348` |
| MD5 | `identify_rom.py` | `3b8585ed03e8ddb89d7de456317545e7` |
| XXH3-64 (librecomp `rom_hash`) | `python -c "xxhash.xxh3_64_intdigest(...)"` | `0x9949AA9BC66F33CB` |
| Header CRC | `identify_rom.py` | `0x5326696F 0xFE9A99C3` |
| Entry point (header +0x8) | `identify_rom.py`; decomp yaml `core` vram | `0x80000400` both ways |
| Other revisions | decomp README | PAL `0B58B8CD/B7B291D2`, SHA-1 `67750e2e…e355` ("F2.6") — out of scope |

### Symbol source

| What | How | Result |
|---|---|---|
| Decomp | GitHub search, `decomp.yaml` | jaytheham/body-harvest-decompilation, `sha1: bbb6666f…` for `us`; IDO 5.3, splat, asm-processor; commit `46006777` (2026-09-15) |
| Matching state | `make` in a WSL mirror | `build/bh.us.z64: OK` — full ROM matches; 1,034 `NON_MATCHING` blocks fall back to `GLOBAL_ASM` |
| Licence | repo root, `gh repo view` | **none published** → used as a build input and for names; nothing from it is copied into this tree |
| ELF | `mips-linux-gnu-readelf -SW build/bh.us.elf` | not relocatable (no `.rel.*`); one section per splat segment |
| FUNC symbols by section | `readelf -sW` | core 687 · frontend 218 · outside 1,444 · inside 237 · greece 64 · java 102 · america 74 · siberia 95 · comet 122 · **3,049 total** |
| Traps present | `readelf -sW` | **181 FUNC with size 0** (hand-written asm, `glabel` emits no `.size`), **6 FUNC ABS** (linker assignments, e.g. `func_80070270`), 788 NOTYPE ABS `func_` assignments from `undefined_syms` — playbook 02 traps, handled in phase 01 |
| Decomp host deps | `pip install -r requirements.txt` on Ubuntu (Python 3.14) | `tree-sitter==0.21.3` / `tree-sitter-languages` have no 3.14 wheel; they serve m2c/permuter only — install the rest, the build is unaffected |

### Code layout

| Segment | ROM | VRAM | Size | Note |
|---|---|---|---|---|
| `core` | `0x1000` | `0x80000400` | `0x3F720` | resident (game + libultra) |
| `overlay_gameplay_frontend` | `0x40720` | `0x80070270` | `0x3EB00` | shares vram with the next two |
| `overlay_gameplay_outside` | `0x7F220` | `0x80070270` | `0xD9110` | outdoor gameplay |
| `overlay_gameplay_inside` | `0x158330` | `0x80070270` | `0x354B0` | buildings |
| `overlay_level_{greece,java,america,siberia,comet}` | `0x18D7E0`… | `0x802D4CD0` | `0x9790`–`0x12F60` | one per world, followed by its (MIO0/RNC) data and alien-type tables |

- Survey block scan (`survey_rom.py`): code-like 4 KB blocks end at ROM `0x329000`; later "code" hits
  are low-entropy data. Code is **stored uncompressed**; level data is MIO0/RNC.
- Loader (decomp `src.us/core/loader.c`): `func_800101F0_10DF0(dest, rom, size)` →
  `func_8000FFC0_10BC0` issues `osPiStartDma` in `0x800` chunks and waits on `D_80067F70` after
  each. Gameplay overlays: `loadFrontendData`, `func_80011674_12274`, `func_800117D8_123D8` (all to
  `0x80070270`); levels: `loadLevelCode(level)` with tables `D_80031C18/2C/40`.
  *Inferred:* chunked DMA means a DMA-level hook sees `0x800`-byte pieces, so the announcement belongs
  on `func_8000FFC0_10BC0` (whole range) rather than on `osPiStartDma` (WR64's choice).
- `osMemSize`: no reference in decomp C (`grep`). *Inferred:* 4 MB game; `0x80400000+` free — confirm
  in phase 04 before placing scratch.
- Retained source names: `src/buildings.c`, `src/loader.c`, `src/trigger.c`.
- Call-target floor (flat scan of `0x1000–0x1F9000`): 16,503 `jal`, 2,389 unique in-window targets,
  3,099 `jr $ra` — consistent with the 3,049 ELF functions.

### Microcode, saves, input

| What | How | Result |
|---|---|---|
| Graphics microcode | ROM strings (`survey_rom.py`) | `F3DEX 1.21` and `L3DEX 1.21` |
| Audio microcode | decomp `1050.c` task setup; third-party port's RSPRecomp config | `aspMain`, text at ROM `0x2FF10` (`D_8002F310`), data at `0x3FC60`; declared text `0xF80` — re-measure the real extent (playbook 06 "wrong text_size") |
| Save medium | decomp yaml libultra list | `osEepromProbe/Read/Write/LongRead/LongWrite` (2.0E–G variants); no `osPfs` file calls, only `osPfsIsPlug`/`__osPfsGetStatus` |
| Rumble | decomp yaml | `osMotorInit/Start/Stop` present (pre-2.0I variant) |
| libultra | header `0x0C` = `0x1448`; decomp notes | mix of 2.0I-identical and 2.0E–G functions |
| Players | game knowledge | 1 |
| Timing model | not yet measured | phase 04/08 (`osViSwapBuffer` rate per screen) |

### Existing ports

| Repo | State | Use |
|---|---|---|
| DeltaniumIndustries/BodyHarvestRecomp | empty repository | none |
| DeltaniumIndustries/BodyHarvestDecomp | archived, moved to jaytheham's | none |
| **12feihu/BodyHarvest-PC-Port** ("bh-recomp-toolkit", MIT, July 2026) | playable through level transitions on a hand-edited RT64 and its own Win32 app; no RecompFrontend, no Linux, hand edits in generated code (`#if 0` stubs) and in RT64; README: "very messy… meant to give others a starting point… 99% AI written" | **Findings only**, credited (below) |

Findings taken from it, each to be re-verified here before relied on:

1. RT64's GBI table registers `L3DEX 1.xx` with `GBIUCode::Unknown`; RSP handlers stay null and the
   L3DEX lists run away. Their fix routes L3DEX 1.xx to F3DEX. (→ phase 04/05, scripted RT64 patch)
2. `aspMain` runs at IMEM `0x04001080` (same as every series port) with the stock 16-entry table
   `0x1118, 0x1470, 0x11DC, 0x1B38, …`.
3. `func_8001BCE0_1C8E0` is a hand-coded `osAiGetLength` (reads `AI_LEN_REG`);
   `func_8001F6E0_202E0` a hand-coded `__osPiGetStatus`; `osEepromLongRead` is called with a
   non-multiple-of-8 byte count (`0x1BD`), which trips librecomp's assert.
4. Helper `func_800235E4_241E4` branches back into `__osException` (playbook 02 boundary trap).
5. Wide aspect shows CPU culling against 4:3 (entity bbox cull `func_800703B0_7F360`, terrain LOS
   occlusion `func_800E95BC_F856C`) and no FOV change — the usual widescreen work (playbook 08).

## Negative results

- **"A DMA hook will see each overlay load whole"** — not for this game: the loader splits every
  transfer into `0x800` pieces (read from decomp C, confirmed by the `NON_MATCHING` asm it mirrors).

## Inference

- *Inferred:* the game is a 4 MB title (no `osMemSize` reference). Confirm with a runtime watch.
- *Inferred:* EEPROM is 4 Kbit (a `0x1BD`-byte save read fits in 512 bytes). Confirm from
  `osEepromProbe`'s result handling in phase 05.

## Skeleton

| What | How | Result |
|---|---|---|
| Submodules | `git submodule status` | N64ModernRuntime `cdf5abb` (N64Recomp `81213c1`), RT64 `5473732`, RecompFrontend `b1a1477`, bh-decomp `4600677` |
| Harness sources | copied from Hybrid Heaven (itself a Wave Race copy with the game-specific tabs stripped), renamed `hh` → `bh`; HH's Controller Pak, Konami file table and F3DEX2 HUD rewriter/census left out; Wave Race's `src/overlays.cpp` and `tools/patch_librecomp.py` added | — |
| Configure + build | `cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"` · `cmake --build build` | `body-harvest-recomp.exe` links (clang-cl 22.1.8) |
| `--identify rom.z64` | exe | "This dump matches the pinned target." exit 0 |
| `--identify` Hybrid Heaven dump | exe | three problems listed, exit 2 |
| Stack patches | `python tools/patch_all.py` twice | first run patches RSPRecomp, N64Recomp (3 files), librecomp `overlays.cpp`, runtime shutdown, recompinput players, RT64 event filter / inspector hook / F2 / texture packs / pairing; second run: everything "already patched" |

**Wrong turn:** HH's `runtime-shutdown.patch` is written against Daniel's runtime fork (hunk at
`is_paused()`, line 172); against upstream `cdf5abb` the right diff is Wave Race's (line 147). Wave
Race's script imports its helper from `patch_rt64_water.py`, which this port does not have; HH's script
(uses `patchlib.py`) with Wave Race's diff is the combination that applies. Pilotwings' diff is
byte-identical to Wave Race's.

## Consequence for the plan

Route A (decomp ELF, PW64 pipeline) with WR64's overlay machinery (`use_lookup_for_all_function_calls`,
runtime function table, resident registration) and the announcement on the game's own loader
(HH pattern). Decisions D1–D9 in PLAN.md.
