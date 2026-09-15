# Phase 06 findings — frontend

**Gate:** met as far as it can be without a person; the "stranger with a pad, and with the keyboard and
no pad" half is Daniel's playtest. Launcher, tabs, single-player input, F1 panel with a live feed all
shown working in sandboxed runs.

Reproduce with:

```
cmake -B build -DBH_WITH_FRONTEND=ON && cmake --build build
python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --seconds 9 --grab 4 --out <dir> --seed tools/test-seeds/graphics.json
python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --rom rom.z64 --seconds 30 --grab 27 --out <dir> --seed tools/test-seeds/graphics.json --env BH_TEST_INSPECTOR=20
```

## Answer

Hybrid Heaven's frontend (Wave Race 64's, game-specific tabs removed) needed only identity, one player
and rumble comments. What did not carry over was the F1 panel's feed and the HUD rewriter: both walk
display lists, and Hybrid Heaven's walk F3DEX2. They were rewritten for F3DEX 1.x against RT64's own
decoders (`include/bh/gbi_f3dex.h`), and the panel lists the intro's 113 elements live.

## Measurements

| What | How | Result |
|---|---|---|
| Frontend build | `cmake -B build -DBH_WITH_FRONTEND=ON` | links; assets staged (fonts, icons incl. new `Logo.svg`, promptfont, `recomp.rcss`) |
| Launcher, no dump | sandbox, grab at 4 s | "Body Harvest: Recompiled", Load ROM / Controls / Settings / Mods / Quit, two-saucer emblem, `v0.1.0` |
| Settings tabs | sandbox without a seeded `graphics.json` | General (Rumble Strength, Joystick Deadzone, Background Input Mode), Graphics (Resolution, Downsampling, Aspect Ratio Expand, Window Mode, Framerate Display, MSAA 2×, HUD Placement 16:9), Sound, Controls, Mods |
| First run | same | "no saved graphics settings; defaulting to fullscreen" — the sandbox run took the whole display, which is why `tools/test-seeds/graphics.json` (windowed) now seeds every test |
| Player assignment | log, no pad attached | `player 1 profiles: controller 1, keyboard 0`; `0 controllers connected; assigned to 1 player` |
| Menu toggle | recompinput defaults (`input_mapping.cpp:29,57`) | Escape; pad `SDL_CONTROLLER_BUTTON_BACK` |
| Game through the frontend build | sandbox, dump on the command line, `start-spam.txt` | same path as phase 04 to Greece (difficulty select seen at 45 s) |
| F1 panel | `BH_TEST_INSPECTOR=20`, grab at 27 s | "Body Harvest HUD", frame 1516, **113 elements** (intro text tiles `tex:0x040928f0#d53901f3` … with x/y extents, `ortho rect`, class dropdowns, outline on the selected rows); "Game editor" window alongside |
| Feed announcement | log | `HUD inspector: first frame with 2D elements: 44` |

## Negative results

- **"F1 resizes the window"** — one run's grab at 67 s came back 1920×991 after a test F1 at 60 s.
  An A/B (same run with and without `BH_TEST_INSPECTOR`, grabs at 18/23/27 s) kept 1200×900 both ways
  with the panel open. Not reproduced; *inferred* to be outside input during that run (Background Input
  Mode is on and the machine was in use).
- **`tools/capture_frames.py`** saved 0 frames of this window (Windows Graphics Capture found the window
  but delivered no frames). `test_sandbox.py --grab` (GDI grab by process id) added instead.

## Not established (Daniel)

- Load ROM through the file dialog with a fresh profile.
- Keyboard play with no pad; a real pad; rumble felt at the slider's strengths.
- The panel's class dropdown applied in gameplay (the rewriter path is untested until a class is set).

## Consequence for the plan

Phase 07 can use `BH_DL_CENSUS` (ported with the feed) to measure the frame before touching widescreen.
