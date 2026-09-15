# Phase 03 findings — runtime harness

**Gate:** met. The executable reaches `recomp_entrypoint` and creates the game's threads, first run:

```
[bh] runtime initialised; entering recomp_entrypoint
[bh] registered 67 runtime-provided and 620 resident functions; loader wrapped at 0x8000FFC0
[bh] game thread 1 created ...  [bh] game thread 7 created
```

Reproduce with `powershell -File tools/boot_runs.ps1 -Runs 10 -Seconds 12` (the log goes to
`%LOCALAPPDATA%\body-harvest-recomp\bh.log` when started without a console).

## Answer

Hybrid Heaven's harness (itself Wave Race's) with Body Harvest's identity, EEPROM save type, a Rumble
Pak in port 1 and Wave Race's overlay registration compiled and linked on the first attempt. The
harness needed no game-specific change to reach the entry point; everything after that is phase 04.

## Measurements

| What | How | Result |
|---|---|---|
| Configure + build | `cmake -B build -G Ninja ... -DBH_WITH_RECOMPILED=ON -DBH_WITH_RUNTIME=ON` · `cmake --build build` | 588 steps, links first time |
| Reaches entry point | log | yes, first run |
| Threads created | log | 7 (entries include the scheduler thread `0x800680A0` argument) |
| Resident functions registered | `src/overlays.cpp` | 620 (later 624 after the gap fill in phase 04), 67 runtime-provided |
| Audio device | log | 48000 Hz, 512-frame period; the game opens its stream at 32000 Hz, resampled in the port |
| First display list | log | ucode `0x8002DEE0`, data `0x8003E860` |

## What was changed from the copied harness

| File | Change | Why |
|---|---|---|
| `include/bh/rom.h`, `tools/identify_rom.py` | Body Harvest identity, XXH3 `0x9949AA9BC66F33CB` | — |
| `src/main.cpp` | `SaveType::Eep4k`; no Controller Pak storage; `register_overlays()`; `set_message_queue_control` defaults | EEPROM game; playbook 05 (nothing installs the control) |
| `src/callbacks.cpp` | one player; port 1 reports `Pak::RumblePak` (`BH_NO_RUMBLE_PAK=1` → none); `aspMain` text at `0x8002F310`; Controller Pak flushes removed | the game drives rumble through `osMotor*`, which the runtime owns |
| `src/overlays.cpp` (new, WR64 + HH) | resident registration, runtime function table, loader wrapper on `func_8000FFC0_10BC0`, unload of the frontend overlay the boot megabyte registers at `0x8003FB20` | D6 |
| `src/inspector.cpp` | HH's promoted HUD tags removed | they are Hybrid Heaven identities |
| `tools/patch_librecomp.py` | miss hook renamed `bh_report_lookup_miss` | links against this port's crash handler |

## Negative results

- **`set_message_queue_control` defaults** fixed nothing visible here (phase 04's two-frame stall is
  elsewhere); kept because the all-clear bitset is wrong in principle (playbook 05).

## Consequence for the plan

Phase 04: the first run stops at a lookup miss (`0x8001F8B0`, see phase-04.md).
