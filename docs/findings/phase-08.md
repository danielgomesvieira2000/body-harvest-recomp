# Phase 08 findings — high frame rate

**Gate:** measured met; artefacts in motion need Daniel's eyes (camera cuts, vehicles, cutscenes, a
display above 60 Hz). No port-side matrix tagging was needed so far.

Reproduce with:

```
python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --rom rom.z64 --seconds 124 \
    --burst 116,60 --out <dir> --seed tools/test-seeds/graphics.json \
    --env BH_INPUT_SCRIPT=<walk script> --env BH_PAIRING=1 --env BH_WINDOW_SIZE=960x720
python tools/frame_motion.py <dir>/burst --region 0,500,400,900 --quiet
```

(The walk script presses through the menus and dialogue, then holds the stick forward and turning;
it lives in the session's work folder, `tools/scripts/walk-greece.txt` in the repository.)

## Answer

RT64 interpolates Body Harvest without help: the game runs outdoor gameplay at 20 frames per second
and RT64 presents 60 on the 60 Hz laptop, pairing all but about one transform per frame. A burst of
window grabs while walking shows the ground changing in 90 % of consecutive grabs with even steps under
Framerate Display, against 47 % with a stepped pattern under Framerate Original.

## Measurements

| What | How | Result |
|---|---|---|
| Presentation mode | `src/frontend.cpp` | `PresentEarly` (Wave Race's; `BH_PRESENT_MODE`) |
| Game rate vs presented, gameplay | `BH_FRAME_STATS=1` (`src/framestats.cpp`, counts `osViSwapBuffer`) | "the game is running at 20 frames per second; presenting at 60 (display 60 Hz)", every window |
| Pairing, dialogue scene | `BH_PAIRING=1` | 53 transforms a frame, 0 ignored, 1.0 unpaired (0.0 moved or new) |
| Pairing, walking and turning | same | 28–41 transforms a frame, 1.0–1.3 unpaired (0.0–0.2 moved or new) |
| Motion, Framerate Display | `--burst 116,60` (36.5 grabs/s), ground region | 53 of 59 pairs changed (90 %), coefficient of variation 0.23 |
| Motion, Framerate Original | same, seeded `rr_option: Original` (29.7 grabs/s) | 28 of 59 pairs changed (47 %), coefficient of variation 2.14 |

Reading the motion numbers (tools/frame_motion.py): at 20 game frames on a 60 Hz display without
interpolation two of every three presented frames repeat the previous one, so a capture sees many
identical pairs and uneven differences; with interpolation nearly every pair differs by a similar amount.
The grab rate (30–36 a second) is below the presented 60, so the fractions, not individual pairs, are
the evidence.

## Negative results

- **"Terrain is rebuilt every frame and will step while the camera glides"** (the Wave Race water
  pattern, playbook 09) — not seen: the ground region changes in 90 % of pairs with even differences.
  *Inferred* from that: the terrain's vertices are stable in world space under a per-frame matrix.

## Not established (Daniel)

- Camera cuts and cutscenes (a wrongly paired cut shows as a smear for a frame or two).
- Vehicles, aliens and effects in motion; the radar and HUD (2D is not grouped as no-interpolate; a
  2D element that pairs with a different one would slide).
- A display above 60 Hz.
- Cost on the Iris Xe at 60 presented frames.

## Consequence for the plan

No matrix groups are emitted. If Daniel sees an artefact, the pairing log and ids go where it is seen
(playbook 09), not everywhere.
