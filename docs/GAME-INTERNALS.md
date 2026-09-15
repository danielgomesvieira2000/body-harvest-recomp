# Body Harvest internals

What the game turned out to be, for anyone working on this port, on a
decompilation, or on another port of a game on the same engine. Names are the
decompilation's at the pinned revision, or `func_XXXXXXXX` / `D_XXXXXXXX`
placeholders, which are also addresses. How the port deals with each fact is in
[PORTING.md](PORTING.md); how it was found is in [findings/](findings).

Conventions: every number names the tool or switch that measures it; refuted
hypotheses are kept; inference is marked *inferred*.

## Identity

| | |
|---|---|
| Title | Body Harvest (USA), game code `NBHE` |
| SHA-1 | `bbb6666f5014a473747ee4145f036d9fb25d7348` |
| XXH3-64 | `0x................` (what librecomp checks) |
| Header CRC | `0x........ 0x........` |
| Entry point | `0x........` |
| Save type | |
| Graphics microcode | |
| Audio microcode | |

## Memory layout

| Region | ROM → VRAM | Notes |
|---|---|---|
| | | |

## The main loop and game states

## Timing

## Rendering

## Audio

## Input

## Saves

---

## Keeping this current

This file describes the *game*. When a later change to the port discovers a new
address, table, format or drawing convention -- or corrects one written here --
update this file in the same commit that makes the change. Facts about N64Recomp,
librecomp, ultramodern or RT64 belong in [PORTING.md](PORTING.md) instead.
