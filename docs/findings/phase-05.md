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

## Consequence for the plan

Departure: phase 06 before phase 05's gate. The checks above are asked for in the same playtest as
phase 06's.
