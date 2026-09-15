# Phase 09 findings — Linux, packaging, documentation

**Gate:** a clean clone builds with the documented commands on both platforms; packages contain no game
data. Status at the end of this record.

## Documentation

README, BUILDING, CONTRIBUTING, THIRD_PARTY_NOTICES, LICENSE (MIT, with the GPL-3.0 note for binaries),
CHANGELOG (Unreleased) and the committed `CLAUDE.md` (commit `744028f`). `licenses/` holds the texts
`tools/third_party_licenses.txt` lists; both packagers refuse to run when one is missing.

`tools/setup_linux.sh` and `tools/build_linux.sh` came from Hybrid Heaven. Changes: no splat step (the
ELF comes from the decomp build), `rsync` added to the packages, and the decomp submodule initialised
**without** `--recursive` (its `tools/n64` gitlink has no `.gitmodules` entry, and a recursive update
fails with `No url found for submodule path`). `build_linux.sh` runs `tools/regenerate.sh` when there is
no generated code.

## Linux build (WSL Ubuntu, clang-21)

`bash tools/build_linux.sh rom.z64` on the working tree: regenerate, configure, 761 build steps,
`Built: build-linux/body-harvest-recomp`.

## The WSLg audio crash

**Run 1** (WSLg, llvmpipe Vulkan, `BH_INPUT_SCRIPT=tools/scripts/start-spam.txt`, temporary
`XDG_DATA_HOME`): title, name entry, Greece gameplay. About 57 s in:

```
Unhandled jump target 0x0000 in microcode aspMain_run
[bh] the game rewrote an audio command list while its task was running
[bh-audio] task failed with UnhandledJumpTarget: 50368 commands at 0x801956F0
```

then exit 139 (SIGSEGV). The Windows build had never shown it.

**First hypothesis, wrong:** the failure bisection (`bisect_audio_task`) re-runs every prefix of the
failed list, and 50,368 commands would take it a very long time. It now skips lists over 4,096 commands
(kept: the cost is still quadratic). Run 2 printed `not bisected` and still died with exit 139.

**Where it died.** There was no gdb in WSL and installing one needs sudo, so `src/crash_handler.cpp`
got a POSIX handler (accessed address, executable base, `backtrace_symbols_fd`). Run 3:
`llvm-symbolizer-21` resolved the first game frame to `func_8000091C_151C` (`funcs_13.c:2386`), called
from the audio thread `func_80000730_1330` — the game's own code, reading `arg1->numFrames`.

**Cause, by reading the decomp** (`core/1050.c`, a matching function). The game builds each audio task
there:

- `outLen = (s16)((D_800431A8 − (osAiGetLength() >> 2) + 0xB0) & 0xFFF0)`, raised to a minimum with a
  signed compare;
- `alAudioFrame` writes the command list into one of two `0x8000`-byte buffers (4,096 commands);
- `data_size` is whatever length it returned.

A task of 50,368 commands can only come from a huge `outLen`. The s16 wraps to a large **positive** length
once the reported depth exceeds about 33,000 frames. On hardware `osAiGetLength` never exceeds the buffer
being played. Here it reports the host's SDL queue, which has no upper bound when the device stops taking
samples. The list then overruns its buffer into the heap, and the audio thread faults on a pointer read
from it a few frames later. *Inferred:* WSLg's audio device stalled under the CPU load of software
rendering.

**Reproduced on Windows** with a device that drains slowly: SDL's disk driver,
`SDL_AUDIODRIVER=disk SDL_DISKAUDIODELAY=1000` (512 frames a second), same script, build without the
fix. It crashed during boot with `ACCESS_VIOLATION` in `func_8000091C_151C + 0xDD`
(`funcs_13.c:2386`, thread `bh_5`), the same place as on Linux.

**Fix** (`src/callbacks.cpp`, port side):

1. `get_frames_remaining` reports at most 4,096 frames. The game's subtraction then goes negative and it
   uses its minimum length.
2. `queue_samples` drops buffers while the device already holds more than a second:
   `[bh] audio device is not draining: N buffers dropped`.

| Run | Result |
|---|---|
| Windows, disk driver, before | crash in `func_8000091C_151C` during boot |
| Windows, disk driver, after (90 s) | `not draining` logged; title, menus, Greece gameplay at 20 frames/s (grab at 75 s: the "Good luck!" briefing over the field); no failed task, no crash |
| Windows, normal device, after (75 s, `BH_AUDIO_STATS=1`) | no failed task; 35 of 36 two-second windows with nothing zero-filled (one at start-up, 3.0 %); queue 18–57 ms, as before |
| WSLg, after | Greece gameplay for over 11 minutes, no crash. The device did not stall in this run (no `not draining`), so this run shows no regression but does not test the fix; the Windows A/B does |

No `BH_NO_…` switch: the cap only applies when the queue is already far beyond anything the game produces
normally (the queue averages ~1,700 device frames).

**Also seen:** on Linux the SIGTERM from `timeout 150` did not end the gameplay run; a second SIGTERM
did. Scripted Linux runs should use `timeout -k 10`. Not investigated.

## Packaging

Both packagers were run with version `0.0.0-test` into temporary folders, then deleted:

| | Archive | Debug symbols | Contents |
|---|---|---|---|
| Windows (`tools/package_release.ps1`) | 17.9 MB zip | 20.9 MB zip | exe, SDL2/dxcompiler/dxil DLLs, `assets/`, `licenses/`, LICENSE, THIRD_PARTY_NOTICES, README |
| Linux (`tools/package_release.py`) | 7.0 MB tar.gz | 31.2 MB tar.gz | executable, `body-harvest-recomp.sh`, README-LINUX.txt, `assets/`, `licenses/`, LICENSE, THIRD_PARTY_NOTICES, README |

No ROM, ELF, save or extracted asset in either archive (listing checked); `git ls-files` has no
`.z64/.n64/.v64/.elf`.

## Clean clone

**Linux** (WSL, `git clone <working tree> ~/bh-clean-clone-test`, then `bash tools/setup_linux.sh` and
`bash tools/build_linux.sh /mnt/c/.../rom.z64`, exactly as BUILDING.md §6):

1. Submodules initialised as documented: the decomp non-recursive, the other three recursive, at the
   pinned commits.
2. **Failed** at ELF verification: `FileNotFoundError: 'rom.z64'` in `verify_elf.py`.
   `build_linux.sh` copies the dump into the repo and calls `regenerate.sh rom.z64`. The relative path
   survived until `wsl_build_elf.sh` had changed into the decomp mirror. The working tree never showed it:
   its generated code already existed, so `build_linux.sh` skipped the regenerate stage.
   **Fix:** `wsl_build_elf.sh` makes the dump path absolute on entry.
3. With the fix: `build/bh.us.z64: OK`, `end state: 3045 FUNC, 0 ABS, 0 size 0, 0 duplicate
   addresses`, `ELF verified`, `functions emitted : 2919 (expected 2919)`,
   `Built: build-linux/body-harvest-recomp`. The clone's executable `--identify` reports
   "This dump matches the pinned target." (A session restart interrupted the first build pass; the
   second pass finished the remaining 293 steps in the same tree.) The clone was deleted.

**Windows** (`git clone <working tree> %TEMP%\bhwin`, BUILDING.md §1–4 verbatim, commit `6942e29`; the
path bug above is not on this route, because `regenerate.sh` gets no argument):

| Step | Result | Elapsed |
|---|---|---|
| submodules (decomp non-recursive, others recursive) + `patch_all.py` | pinned commits, patches applied | 132 s |
| `identify_rom.py` | matches | |
| `wsl -d Ubuntu -e bash tools/regenerate.sh` | `OK`, `3045 FUNC, 0 ABS…`, `ELF verified`, `2919 (expected 2919)` | 90 s |
| configure + build | `body-harvest-recomp.exe` | 207 s |
| `body-harvest-recomp.exe --identify rom.z64` | "This dump matches the pinned target." | total 430 s |

The clone's exe in `test_sandbox.py` (45 s, `start-spam.txt`): audio audible, backdrop stretched, grab at
38 s shows name entry; no crash. The clone was deleted.

## Gate

| Condition | Status |
|---|---|
| A clean clone builds with the documented commands, Linux | **met** after the `wsl_build_elf.sh` fix |
| A clean clone builds with the documented commands, Windows | **met** |
| Packages contain no game data | **met** (both packagers, contents listed) |
| Repository and first release | not done: only when Daniel asks (D10) |
