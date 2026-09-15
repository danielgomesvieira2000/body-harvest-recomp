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

## The player model (after Daniel's first look)

**Reported:** "The player model during gameplay must get a fixed interpolation on the model itself, it
looks jittery."

**How the game draws him** (decomp F9230.c `func_800EF14C`, confirmed by dumping the list):
- three matrices, a base scale (LOAD), position and heading (MUL), root pose (MUL);
- segment 7 set to fifteen bone matrices built this frame (`func_8000CC3C`);
- `gSPDisplayList(0x010031E0)` (Black Adam `0x050408F0`). Inside it the torso list is drawn under the
  caller's matrices. Then each part is `G_MTX 0x04` (modelview, multiply, push) of
  `0x07000000 + 0x40·n`, the part's list, and pops back up the chain (`BD`).

Every part is its own transform, paired by RT64's call signature and nearest position: the Wave Race
riders' case (playbook 09).

**Fix** (`src/hudrewrite.cpp`):
- The model call is inlined, and an explicit `gEXMatrixGroup` goes before each bone matrix: id from the
  bone's segmented address and the model, `G_EX_ORDER_LINEAR`, decomposed, translation always interpolated.
- The torso gets its own transform: a multiply by an identity matrix (scratch `0x80780000`) under the
  torso's id at the start of the model.
- RT64's defaults are restored after the call.
- The rewriter now runs every frame when this is on, not only when a 2D class is set.

**Verified by Daniel watching two scripted runs** (`walk-greece.txt`, 16:9):
- with ids the model was steady; with `BH_NO_MODEL_IDS=1` it jittered;
- `BH_PAIRING` counters were no guide: 1.1–2.0 unpaired a frame both ways (the defect is wrong pairs, which count as paired).

## The player's shadow

**Reported next:** the shadow under the player jitters.

**How the game draws it** (decomp F7870.c `func_800E988C`): a quad of five vertices (four corners and
the centre) computed in world coordinates every frame, loaded with `gSPVertex(5)` and drawn with two
`gSP2Triangles`. It sits under the world matrix every shadow shares. That matrix never moves, so the
geometry steps at 20 frames while the player glides (playbook 09, Wave Race's sky and water).

**Fix:**
- The quad gets a transform of its own: identity multiply, then a group with vertex interpolation
  (`G_EX_COMPONENT_INTERPOLATE`), linear ordering and a fixed id.
- Another identity multiply under RT64's defaults closes it after the triangles.
- The quad is the same five points in the same order every frame, so pairing vertices by index is sound.
- A move over 400 units in one game frame is not interpolated.

**First attempt, wrong (kept):**
- It found the quad by its centre being exactly the player's position (`*D_80052B34`). Daniel saw no
  change in either run.
- A trace of small vertex loads near the player showed why. Standing, the centre matched. Walking, it
  trailed the position read at submission by 10–14 units, because the game has already moved him on by
  then. So the match held only while he stood still.
- The one-time "found" log line had come from a standing frame.

**Second attempt:** a five-vertex load whose fifth vertex is the mean of the four corners (±2), within 96
units of the player. `BH_SHADOW_TRACE=1`: found in 100 of 100 lists, walking and turning. Daniel:
the jitter is gone with it and back with `BH_NO_SHADOW_INTERP=1`.

## The aiming reticle

**Reported:** "I would like to implement matrix interpolation on the aiming crosshair. It jitters when I
aim around."

**How the game draws it:**
- The reticle is a camera-facing billboard at the aim point, scaled with distance, built in
  `func_800A2D98_B1D48` and emitted by `func_800A2260_B1210`.
- It is nine vertices (centre, corners, edge midpoints) in world coordinates under the static world
  matrix `0x80031160`, preceded by `SETTIMG 0x01009A70`, then one quad and three triangle pairs.
- `func_800A2B58_B1B08` ("ghost target") draws the same vertices again, fainter.

It is the shadow's case: geometry rebuilt under an unchanging matrix, stepping at 20 frames.

**Fix:** the shadow's group, reused.
- Found by that texture followed by a nine-vertex load.
- One id per occurrence in the frame (the scripted aim run found 2).
- Vertex interpolation, linear order.
- A centre jump over 2,000 units (a new target) is not interpolated.
- `BH_NO_RETICLE_INTERP=1`: off.

**Verified:** Daniel, aiming around: "The crosshair fix worked."

**Not covered:** other characters' and vehicles' models and shadows (same two mechanisms; aliens and
civilians go through `func_8007C044` and the same shadow function). Daniel, in play (2026-09-15): "so
far they don't seem to be a problem". Left as RT64 pairs them unless an artefact is seen.

## Consequence for the plan

No matrix groups were emitted at first; ids went where Daniel saw an artefact (playbook 09), not
everywhere: the player model and his shadow (sections above).
