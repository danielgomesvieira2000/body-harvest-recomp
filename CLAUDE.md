# Working in this repository

Body Harvest: Recompiled — a static-recompilation PC port of Body Harvest (USA)
(game code `NBHE`, SHA-1 `bbb6666f5014a473747ee4145f036d9fb25d7348`). Read the docs map below before
changing anything you have not touched before.

## Docs map

| File | What it holds |
|---|---|
| `docs/PLAN.md` | phases, gates, standing constraints |
| `docs/findings/phase-NN.md` | the working record per phase, wrong turns included; not rewritten |
| `docs/PORTING.md` | port facts: toolchain, runtime, renderer, patches (symptom first) |
| `docs/GAME-INTERNALS.md` | game facts: addresses, tables, formats, drawing conventions |
| `docs/BUILDING.md` | step-by-step build |
| `docs/issues/NNN-*.md` | one file per visual/behaviour bug, from `docs/issues/TEMPLATE.md` |
| `docs/releases/X.Y.Z.md` | short release notes; `CHANGELOG.md` indexes them |

**Update the affected reference doc in the same change that makes it wrong** —
game facts in GAME-INTERNALS.md, port facts in PORTING.md. Record the measurement
and the switch that re-derives it; keep negative results; mark inference as inference.

## Hard rules

- **Never commit game data.** No ROM, ELF, extracted or derived asset. Do not work
  around `.gitignore`.
- **Never edit `RecompiledFuncs/` or `RecompiledPatches/`.** They are generated.
  Changes go in `recomp/*.toml`, `patches/`, or a script in `tools/`.
- **Never hand-edit a submodule** (`lib/`). Every change is an idempotent
  `tools/patch_*.py`, run by `python tools/patch_all.py`.
- **Never change the decompilation** (`lib/bh-decomp`); it is a mirror.
- **One revision only**: every address is tied to SHA-1 `bbb6666f5014a473747ee4145f036d9fb25d7348`.

## Build

| Directory | What |
|---|---|
| `build/` | Windows, clang-cl + Ninja, RelWithDebInfo (Debug breaks audio timing) |
| `build-linux/` | Linux / WSL (`tools/build_linux.sh`) |
| `lib/N64ModernRuntime/N64Recomp/build-linux/` | N64Recomp + RSPRecomp, built under WSL (`tools/wsl_build_recompiler.sh`) |

Regenerate after a submodule or config change: `python tools/patch_all.py`, then
`wsl -d Ubuntu -- bash tools/recompile.sh`, then rebuild.

## Environment variables

All port switches use the prefix **`BH_`** (e.g. `BH_INPUT_SCRIPT`,
`BH_AUTOSTART`). A change that could regress on someone else's machine
gets a `BH_NO_...` switch. List them in PORTING.md.

## How to work

- **Land one verified change at a time.** When several mechanisms affect the same
  pixels, change one and confirm it before the next.
- **Verify by running the game**, with scripted input and `tools/capture_frames.py`,
  not only by reading code or grabbing one frame.
- **Timebox investigations.** If a few measurements do not converge, write down
  what was measured and check in.
