# Phase 05 findings — graphics, audio and saves correctness

**Gate:** not met yet — met with a departure. Everything measurable without a person is done and
recorded here. The gate's exit ("Greece from the start through the first building and a save/load,
correct visuals and audio, confirmed by Daniel in play") needs Daniel: rumble felt, audio pitch heard,
a building entered, a level change. Phase 06 starts before that gate so the playtest uses the real
launcher and settings (same departure Hybrid Heaven took; PLAN.md *Departures*).

Reproduce with:

```
set BH_INPUT_SCRIPT=tools\scripts\start-spam.txt
set BH_AUDIO_STATS=1 & set BH_FRAME_STATS=1
powershell -File tools/boot_runs.ps1 -Runs 1 -Seconds 80
powershell -File tools/shoot_run.ps1 -Count 18 -Interval 5 -Delay 4 -OutDir <dir>
```

## Measurements

| What | How | Result |
|---|---|---|
| Visuals, boot to gameplay | `shoot_run.ps1` grabs, 4–89 s | Midway logo, intro text, title (ship model, flame logo), slot select, name entry, Play menu, dropship cutscene, Greece: terrain, trees, buildings, HUD bars, radar, dialogue box with portrait, truck entered and driven. Nothing visibly wrong at 4:3 |
| EEPROM write | settings folder after the run | `saves/bh.us.0.bin`, 512 bytes, written during name entry |
| EEPROM survives restart | second boot, START ×4, grab at 19 s | slot 1 reads "AAAAAA Greece 0"; slots 2–3 "Empty" |
| EEPROM read path | `BH_TRACE_FUNCS=0x8001D5A0,...` | `osEepromProbe` → 1 (4 Kbit); long read of `0x1BD` bytes = 56 `osEepromRead` blocks, ~16 ms apart (phase 04) |
| Rumble Pak | trace | `osMotorInit` → 0; the game prints "Rumble pak(tm) detected!" path (decomp `1050.c`) |
| Audio production | `BH_AUDIO_STATS=1`, 2-s windows in gameplay | 60 buffers/s of ~800 frames (792–1008) at the game's 32000 Hz, resampled to 48000 |
| Audio queue | same | trough 855–951, average ~1,680, peak ~2,750 frames at 48 kHz (17.8 / 35 / 57 ms); device period 512; "nothing zero-filled" in every window |
| Audio content | same | peak amplitude 3,660–5,069 of 32,767 (non-silent, moving with content) |
| Resampler cost | same | 0.54 % of one core |
| Game frame rate, gameplay | `BH_FRAME_STATS=1` | 20 display lists/s (one frame per 3 VIs); 60/s in the intro and menus |

## Not measured (needs Daniel)

- Audio pitch and quality by ear (compare against an emulator or hardware).
- Rumble felt, and its fade timing (the controller loop rate, `BH_SI_LATENCY_MS`, phase 04 inference).
- A building (`overlay_gameplay_inside`), a level change (Greece → Java, `overlay_level_java`).
- Loading the save into play (the slot is listed; the game was not continued from it).

## Inference

- *Inferred:* the 20 fps gameplay rate is the game's own design, not a port defect: decomp `FD80.c`
  sets the frame's VI count `var_v1 = 3` outside menus and `1` in the frontend modes, and the port
  measures exactly 20 and 60.

## Rumble (after Daniel's playtest)

**Reported:** "It seems like the rumble is not working."

**Measured, in order:**

| Question | How | Result |
|---|---|---|
| Does the harness pass rumble on? | read `src/callbacks.cpp`, `src/frontend.cpp` | `has_rumble_strength`, `set_rumble` → `recompinput::set_rumble`, `update_rumble()` each frame, port 1 reports a Rumble Pak: complete |
| Does the runtime? | read ultramodern `input.cpp` | `osMotorInit` marks the pak initialised; `osMotorStart/Stop` call `set_rumble(channel, flag)` |
| Does the game call it? | `BH_TRACE_FUNCS=0x8001CA80,0x8001C798,0x8001C630` while firing | init → 0; `osMotorStop` called constantly, `osMotorStart` in gameplay, both → 0 |
| Why is it not felt? | decomp `1050.c` | see below |

**Cause:** the game sets strength by pulse density. Its rumble logic `func_80001190_1D90` runs once per
controller-loop pass:
- it adds `(intensity >> 4)³ / 512` to an accumulator;
- it queues `osMotorStart` when the accumulator reaches 256 (and subtracts 256), `osMotorStop` otherwise;
- the rumble thread `func_80000ED4_1AD4` turns the queue into motor calls.

So strength is the fraction of passes with the motor on. recompinput models an on/off motor sampled once
per rendered frame, ramping +0.17 while on and decaying ×0.92 while off. A sample of a pulse train is a
random instant of it, so rumble barely got through.

**Fix** (`src/callbacks.cpp`):
- `set_rumble` records when the motor goes on and off.
- Once per frame the main thread turns that into duty (on-time ÷ elapsed time).
- The duty is low-passed like a motor (40 ms up, 80 ms down, judged by feel), times the Rumble Strength
  slider, and zero when muted out of focus.
- It goes to both of the pad's motors with `SDL_GameControllerRumble`, sent on a change of ≥ 0x800 or
  refreshed every 100 ms.
- recompinput's `update_rumble` is no longer called; it would overwrite the strength.
- `BH_RUMBLE_RAW=1` restores the old path; `BH_RUMBLE_TRACE=1` logs motor calls per second, mean duty,
  peak level and the last strength sent.

**Result:** Daniel: "rumble is working correctly now".

**Open, measured with `BH_RUMBLE_TRACE`** (resolved in "The controller loop" below): the controller loop makes 23,000–28,900 motor calls a
second, one per pass, so it runs ~25 kHz. The 1 ms SI latency glue (phase 04) should hold it near 1 kHz,
so it does not throttle this loop as intended.

Consequences:
- The game's rumble hold and fade counters (`D_80047688`, `D_8004768C` against 10,001 and 801 passes)
  run out many times faster than on hardware. *Inferred:* rumble events are shorter than intended.
- One message and thread switch per pass costs CPU.

The hardware loop rate is not known here. Not changed yet.

## The controller loop

**Found by the rumble trace:** the controller loop made 23,000–28,900 passes a second. The glue meant
to pace it at 1 ms per `osContStartReadData` (phase 04) evidently was not.

**Measured** with `BH_SI_TRACE=1` (reads per second, and the time each block really lasted):

| Test | Reads/s | Block, mean | Conclusion |
|---|---|---|---|
| 1 ms (the old default) | ~29,000 | 13 µs | the timer message arrives almost at once |
| 1 ms, pacing queue drained before each arm | ~29,000 | 13 µs, **0** messages drained | not a stale message: refuted |
| 5 ms | ~220 | 4,300 µs | paced, but a millisecond short |

**Cause (runtime, Windows):** the timer thread waits with moodycamel's `wait_dequeue_timed`. On Windows
that is `WaitForSingleObject(sema, (unsigned long)(usecs / 1000))`, truncating the wait to whole
milliseconds. A 1 ms timer is a sub-millisecond wait by the time it is computed, so it fires at once;
any other timer fires up to 1 ms early. Linux waits in nanoseconds, so the platforms differed.

**What the rate should be:**
- The game counts rumble in passes (`func_80001144(intensity, hold, decay)`: hold 5–20 passes, then
  3–20 per pass, so 40–220 passes a rumble; pak re-check every 2,500).
- ares estimates a controller read at `13,600 + 22,000` for a connected pad and `18,000` per empty
  port, `× 3` cycles at 93.75 MHz: ~2.9 ms with four ports polled, plus ~0.13 ms for the SI write.
  That is ~330 passes a second, 0.12–0.66 s per rumble.
- Sources: ares `n64/si/io.cpp` and `n64/pif/hle.cpp` (`estimateTiming`).

**Fix:**
1. `tools/patch_runtime_timer.py` (in `patch_all.py`) rounds the timer thread's wait up to a whole
   millisecond: a timer now fires at most 1 ms late, never early or at once. This affects every game
   timer, including the EEPROM block waits, which now last their full 15 ms.
2. `BH_SI_LATENCY_MS` default 3.

**Result:** 284–290 reads a second, 3.3 ms per block (3 ms rounded up), gameplay 20 fps, rumble peaks
0.73. Daniel, playing: "the rumble worked better now" (at 5 ms), then "This feels correct" (at 3 ms).

**Found during the test, separate:** entering a building crashes (next section).

## Entering a building

**Reported:** "The crashing happens when opening a door of a house."

**Crash 1:**
- `ACCESS_VIOLATION` in `recomp::do_rom_read` under `osPiStartDma`, reached from the loader
  `func_800101F0` ← `func_800105F0_111F0` ← the inside overlay's `loadLevel`.
- `BH_DEBUG_LOADS=1`: overlay `gameplay_inside`, data `0x7E6E50 → 0x802F2EF0`,
  `0x37F840 → 0x800D6460`, then **`0x38F640 → 0x00000001` (4 bytes)**.
- Cause: the game's own code. `func_800105F0_111F0` (core `loader.c`, matching) loads four bytes to
  `*sp28` and returns them, and never sets `sp28`: `lw $a0, 0x28($sp)`.
- On the console that slot holds an address left by an earlier call. In the port the runtime's native
  libultra functions never write the MIPS stack, so it held 1.
- Fix: a wrapper puts a pointer to scratch `0x807EF068` into that slot before the call
  (`BH_NO_LOADER_SLOT=1`: off). The value returned is the four bytes read, as on the console.

**Crash 2** (Daniel: "went a step further by showing a black screen"):
- `ACCESS_VIOLATION` reading in `func_800881C0_170280` + 0x826 (inside overlay, effect billboards),
  from `func_8008B1A8_173268` ← `loadLevel`.
- Decomp: `var_s3 = spAC;` with `spAC` never set, then `var_s3->unk9` on the first pass. The value is
  overwritten later in the loop, so the console only needs the stale pointer to be readable.

Two uninitialised stack reads in one interior load suggested more. Rather than seed each one:
- librecomp reserves 4 GB for RDRAM and maps only the 8 MB of real RAM, so a stale pointer faults
  (Windows `PAGE_NOACCESS`, Linux `PROT_NONE`).
- The crash handler now maps just the faulting 4 KB page read-only (zeros) when a **read** lands
  inside that reservation, logs it once per page with the reading function, and continues. A write
  there still crashes. `BH_STRICT_MEMORY=1`: crash on any access, as before.

**Result:** Daniel entered a house, walked around and left: "That works". The log shows the loader
slot used and exactly one stale read, `N64 address 0xE700000A` from `func_800881C0_170280 + 0x826`.
The slot held a leftover display-list word, `0xE7000000` (`G_RDPPIPESYNC`), plus the field offset.

## The map crash

**Reported:** "The game crashed due to opening the map" (START in gameplay).

**Crash report:**
- `ACCESS_VIOLATION` in `RT64::RDP::loadTileOperation` (`rt64_rdp.cpp:508`), on the Gfx thread;
- reached from `State::fullSyncFramebufferPairTiles` and the port's `send_dl`.

Reproduced by script (`walk-greece.txt` with START at 117 s).

**Hypotheses, in order:**

| Hypothesis | Test | Result |
|---|---|---|
| The rewriter, now running every frame, inlines a list whose segment it never saw | `BH_NO_HUD_REWRITE=1` | still crashes: refuted |
| Replacing `setGameplayResolution` left the screen size unset | `BH_FULL_FRAME=0` | still crashes: refuted |
| Look at the commands RT64 gets | probe: every `G_LOADTILE` with backwards extents | every piece `LOADTILE … uls 0 ult 80 lrs 0 lrt 0` from `TIMG 803DA800` (the second framebuffer) |

**Cause:**
- The pause transition (`func_8000E53C_F13C`, decomp `E830.c`) cuts the last frame into a 16×12 grid
  of tiles sized from `D_8005BAEC/D_8005BAF0` (screen width and height).
- Both were 0. Their only writer, `0x8000E4B0` (callers pass `D_80068084, D_80068088`), is named
  `osSetTime` in the decomp, which is on N64Recomp's runtime list.
- So every call went to ultramodern's `osSetTime`. It set the OS clock to `320<<32 | 240` and never
  stored the size.
- A real RDP loads nothing for a tile whose bottom is above its top; RT64's `rowCount = 1 + (lrt − ult)`
  underflowed and it read gigabytes past RDRAM.

**Fix:** `tools/fix_elf.py` renames that symbol `bhSetSize`, guarded by name, address and size. The
game's own function is recompiled: 2,920 functions emitted (was 2,919).

**Audit:** all 125 runtime-owned names present in the ELF were listed with address and size. `osSetTime`
was the only one in game code (`0x8000E4B0`); every other one sits in the libultra range from
`0x8001ABE0`.

**Result:** the same script, no crash, well-formed tiles (e.g. `uls 1200 ult 720 lrs 1280 lrt 800`).
Daniel: "That fixed the crash."

*Inferred, not measured:* the clock was also set to 320·2³² + 240 each time the size was "set", which
may have disturbed anything timed with `osGetTime` around menus.

## Consequence for the plan

Departure: phase 06 before phase 05's gate. The checks above are asked for in the same playtest as
phase 06's.
