# Porting reference

How this port is put together and why each piece is the way it is: the pipeline
from a dump to an executable, the runtime harness, patches, and the enhancements.
Facts about the game are in [GAME-INTERNALS.md](GAME-INTERNALS.md); the
step-by-step build is [BUILDING.md](BUILDING.md); how each fact was found is in
[findings/](findings).

Each section states the **symptom first**, because a symptom is what you will have
when you come looking.

Pinned upstream revisions:

| Submodule | Commit |
|---|---|
| N64ModernRuntime | `` |
| N64Recomp (inside N64ModernRuntime) | `` |
| RT64 | `` |
| RecompFrontend | `` |

## Contents

1. [The pipeline](#the-pipeline)
2. [The ELF](#the-elf)
3. [Recompiling](#recompiling)
4. [The harness](#the-harness)
5. [Patches](#patches)
6. [Widescreen](#widescreen)
7. [Frame interpolation](#frame-interpolation)
8. [Submodule patches](#submodule-patches)
9. [Testing and diagnostics](#testing-and-diagnostics)

## The pipeline

```
dump.z64 ──▶ body-harvest.elf ──N64Recomp──▶ RecompiledFuncs/ ──CMake──▶ body-harvest-recomp
                               ──RSPRecomp──▶ audio microcode
patches/ ──────────────────────────────────▶ RecompiledPatches/
```

## The ELF

## Recompiling

## The harness

## Patches

## Widescreen

## Frame interpolation

## Submodule patches

Every change to a submodule is an idempotent script in `tools/`, run by
`python tools/patch_all.py`. Rerun it after any submodule update.

| Script | Submodule | What and why |
|---|---|---|
| | | |

## Testing and diagnostics

### Environment variables

| Variable | Effect |
|---|---|
| `BH_INPUT_SCRIPT` | timed input script |
| | |

### Traps

---

## Keeping this current

This file describes the *port*: the toolchain, the runtime, the renderer, and the
techniques used against them. When a later change fixes something in one of
those -- or finds that something written here is no longer true of a newer
submodule -- update this file in the same commit that makes the change, and say
what the symptom was. A finding without its symptom is much harder to find again.

Facts about Body Harvest itself belong in [GAME-INTERNALS.md](GAME-INTERNALS.md).
