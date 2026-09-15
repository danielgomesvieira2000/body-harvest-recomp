# Phase 04 findings — boot bring-up

**Gate:** met. Unattended (`tools/scripts/start-spam.txt`): Midway logo, intro story, slot select, name
entry, the Play menu, the dropship cutscene, and Greece gameplay -- Adam on foot, HUD, radar, dialogue
with portraits, getting into a truck and driving it. The three overlay kinds load through the wrapper:
`gameplay_frontend`, `gameplay_outside` (evicting frontend), `level_greece`. Attract demos were not
waited for (the script presses through them).

Reproduce with:

```
powershell -File tools/boot_runs.ps1 -Runs 1 -Seconds 25 -Env "BH_FRAME_STATS=1"
set BH_TRACE_FUNCS=0x8001BA70,0x8001C3D0   (osRecvMesg, osSendMesg) & set BH_TRACE_LIMIT=100000
set BH_DEBUG_LOADS=1
python tools/symbolize_log.py %LOCALAPPDATA%\body-harvest-recomp\bh.log
```

## Run 1 — lookup miss at 0x8001F8B0

```
[bh] no function registered at 0x8001F8B0
[bh] called from alBnkfNew + 0x2D7   RecompiledFuncs\funcs_5.c:3152
```

**Cause:** `0x8001F8B0` is a `static` helper of libultra's `bnkf.o`. The decomp links libultra objects
from an archive whose statics carry no symbol, so the bytes between `alHeapInit` and `alBnkfNew` were
covered by no FUNC and never recompiled. A gap audit over every code section (scratch script, now
part of `tools/fix_elf.py`) found exactly three such gaps, all in core:

| Gap | Contents | Named by undefined_syms |
|---|---|---|
| `0x8001F8A0`–`0x8001F99C` | two empty `jr $ra; nop` functions, then a 0xF4-byte function, then another empty one | `func_8001F8B0` |
| `0x80024F50` +0x12C | one function (after `alRaw16Pull`) | `func_80024F50` |
| `0x8002C1C4` +0x1DC | two functions (after `__osLeoInterrupt`) | `func_8002C1C4`, `func_8002C2AC` |

**Fix:** `fix_elf.py` rebinds each referenced ABS name that falls in an uncovered gap as a FUNC in that
section, sized to the next boundary with trailing zero words dropped but a `jr $ra` delay slot kept.
Unreferenced empty functions are reported and left out. 3,045 functions; `verify_elf.py` still all zero;
2,919 emitted.

**Wrong turn in the first version:** trimming trailing zero words also removed `jr $ra`'s delay-slot
`nop` (`func_8002C2AC` came out 0xE8 instead of 0xEC). Caught by reading the sizes against the
disassembly before rebuilding.

## Run 2 — two display lists, then a black window forever

VI and audio kept running (600+ screens, audio buffers queued); `BH_FRAME_STATS=1` showed
`display lists in the last 60 updates: 2` at t=1 s, then 0.

Instruments built for this (kept):

- `BH_SAMPLE=1` thread sampler + `tools/symbolize_log.py` (new): every game thread parked in
  `osRecvMesg`; nothing spinning in game code.
- `BH_TRACE_FUNCS=addr,...` (`src/calltrace.cpp`, new): wraps game addresses in the function map and
  prints arguments and results. Works for resident and runtime-provided functions.

### 2a. Scheduler exonerated

Tracing `__scTaskReady` (`0x8001B130`), the task start (`0x8001B180`), task completion (`0x8001B404`),
`osSpTaskStartGo`, `osViGetCurrentFramebuffer/Next`, `osViSwapBuffer`, `osDpSetNextBuffer`: both
graphics tasks start and complete, both framebuffers (`0x80267080`, `0x8028C880`) are swapped, audio
tasks keep running. The graphics thread is not being starved of completions.

### 2b. The EEPROM timer queue nobody created

Tracing the boot thread's calls: `osEepromProbe` → 1 (4 Kbit), then the game's hand-written
`osEepromLongRead` (`func_8001D5A0_1E1A0`, called with `0x1BD` bytes) does one `osEepromRead`, one
`osSetTimer` (~16 ms), and never comes back.

The routine waits between blocks on `__osEepromTimerQ` (`0x8006CA28`). The cartridge's `osContInit`
creates it (instruction at `0x8001CEDC`: `osCreateMesgQueue(&__osEepromTimerQ, &__osEepromTimerMsg, 1)`)
— but `osContInit` is the runtime's. The queue stays zero (0 slots) and the timer message never lands.

**Fix:** `src/libultra_glue.cpp` wraps `osContInit` at `0x8001CD00`: the runtime's implementation, then
the queue exactly as the cartridge creates it. After it, the trace shows all 56 blocks read, then
`osMotorInit` → 0 (Rumble Pak detected), then the controller loop.

**Still two display lists.** Not the whole story.

### 2c. A controller thread that never blocks

Tracing `osSendMesg`/`osRecvMesg` (filtered to the handshake queues `D_8006A908/A8B0/A8D0/A8F0`,
decomp `FD80.c`, `10A20.c`, `1050.c`):

1. The graphics thread renders two frames, then blocks in `osRecvMesg(D_8006A8B0)` waiting for the
   game thread's next frame.
2. The game thread (`func_8000FE50_10A50`, priority 4) has drawn its two start-up frames and blocks in
   `osRecvMesg(D_8006A8F0)` until the boot thread has read the save.
3. The boot thread sends `D_8006A8F0` (`osSendMesg` returns 0) — and the game thread's receive never
   returns.

The sender is the controller thread (`func_80002EF8_3AF8`, priority 5): after the save it loops
`osContStartReadData` → `osRecvMesg(SI queue)` → process → `osContStartReadData`. The runtime completes
an SI transfer immediately, so the SI message is already queued when `osRecvMesg` runs; the receive never
blocks, and ultramodern's scheduler only switches at a blocking wait to a lower-priority thread. On
hardware the transfer takes time and the thread blocks every iteration.

**Fix:** `src/libultra_glue.cpp` wraps `osContStartReadData` at `0x8001D6E0`: after the runtime starts
the read, the calling thread blocks on a timer (private queue at scratch `0x807EF040`) for
`BH_SI_LATENCY_MS` (default 1 ms; mupen64plus raises the SI interrupt ~0x900 count ticks after the
transfer, which the host timer cannot resolve below ~1 ms). `BH_SI_LATENCY_MS=0` restores the old
behaviour.

**Result:** `display lists in the last 60 updates: 60` from t=3 s; the frontend overlay loads
(`[bh-load] overlay gameplay_frontend -> 0x80070270`) followed by data loads; at t=30 s the intro story
("alien invasion has harvested mankind to the brink of extinction…") renders correctly.

## Run 3 — START on the intro crashes RT64

```
[bh] ACCESS_VIOLATION ... while reading address 0x2D204520003
[bh] in function RT64::RDP::loadTLUTOperation + 0x337   (Gfx Thread)
     <- GBI_RDP::fullSync <- Interpreter::processDisplayLists
```

The game draws with F3DEX 1.21 and L3DEX 1.21. RT64 `5473732` lists `L3DEX 1.00/1.21/1.23/1.23 (Variant)`
as `GBIUCode::Unknown`, which gets only the RDP handlers, so an L3DEX list is walked past its end as data
until a byte pattern looks like a TLUT load. This is the fix 12feihu's port documents (routing L3DEX 1.x
to F3DEX); re-derived from this crash and RT64's table.

**Fix:** `tools/patch_rt64_l3dex.py` (in `patch_all.py`): the four entries become `GBIUCode::F3DEX`.
L3DEX's line command (0xB5) then lands on F3DEX's quad slot; nothing visibly missing so far.

**Result:** the contact sheet of 18 window grabs at 5 s intervals, 4-89 s, shows the whole path above.
No lookup misses, no crash in 90 s.

## Negative results

- **Message-queue control defaults** — installed, no change to the two-frame stall.
- **"The scheduler withholds completions"** — refuted by 2a.

## Inference

- *Inferred:* the controller loop's rate matters to rumble timing (`func_80001190_1D90` counts
  iterations: rumble fade after 10,001, Rumble Pak re-check every 2,500). At the 1 ms default the loop
  runs ≤ 1,000 Hz, the same order as hardware if the SI latency is sub-millisecond. To be checked when
  rumble is felt in play (phase 05).

## What is not established

- Attract demos and their speed; the title screen when nothing is pressed.
- Whether L3DEX lines are drawn anywhere (0xB5 as a degenerate quad would hide them).
- The `inside` overlay (a building), a level change, EEPROM write -- phase 05.
