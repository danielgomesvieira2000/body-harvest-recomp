# NNN — one line, what looks wrong

Copy this to `docs/issues/NNN-short-name.md`, put the screenshots next to it, and
fill in what you can. Anything left blank is fine; anything filled in is search
space someone does not have to cover.

Filing it here rather than in a chat message is deliberate: it survives the
session, the images and notes are read together, and once fixed the file is the
regression record that stops it coming back.

**Status:** open | fixed in `<sha>` | not a bug (this is how the game looked)

---

## What is wrong

What you see, and what you expected instead.

If a real N64 or an accurate emulator looks different at this spot, say so. "Port
bug, or how the game looked?" is a real question, and one comparison screenshot
settles it more cheaply than reasoning about the renderer.

## Where

The level or mode, and how to reach this exact spot.

A save parked just before it is worth more than any description: copy it out of
the port's settings folder (`%LOCALAPPDATA%\body-harvest-recomp` on Windows) and drop it
beside this file. A scripted-input file (`BH_INPUT_SCRIPT`) that
reaches it is even better.

## Is it stable?

- Every frame, or intermittent? If intermittent, how many runs of how many?
- Only while the camera moves, or also standing still?
- Only at this spot, or everywhere with this kind of geometry?
- At the game's own frame rate as well as at high frame rate?

## The quick triage

One setting at a time in the Graphics tab; it eliminates most of the search
space before anyone reads code.

| Change | Result | What it means if it changes the defect |
| --- | --- | --- |
| Internal resolution | | scales with it → renderer or upscaling, not the display list |
| Aspect ratio, Expand ↔ Original | | only when widened → culling, or 2D anchoring |
| Frame rate, Original ↔ Display | | only interpolated → pairing or matrix groups |
| Antialiasing off | | gone → a coverage or edge issue |

## The frame itself

Press **F1** for RT64's developer UI. Pause on the bad frame and walk framebuffer
pairs → projections → draw calls; highlighting a call shows which geometry it is.
A screenshot of that panel, or one line -- "draw call 37 in projection 1 is the
water" -- turns a day of guessing into an hour.

## Evidence

- screenshot of what was on screen
- the log from the settings folder, covering the minutes before
- the settings in use

## Investigation

Filled in by whoever works it. What was ruled out matters as much as what was
found -- a negative result recorded here is one nobody has to reproduce.

## Fix

What changed and why, and how it was verified. Ideally: the save or script that
reproduced it, and a note that it now looks right.
