"""Give RT64's L3DEX 1.x microcode entries a command set.

**Symptom:** pressing START on Body Harvest's intro crashes the Gfx thread:

    [bh] ACCESS_VIOLATION (0xC0000005) ... while reading address 0x2D204520003
    [bh] in function RT64::RDP::loadTLUTOperation + 0x337  (rt64_rdp.cpp:584)
    ...  RT64::GBI_RDP::fullSync <- RT64::Interpreter::processDisplayLists

Body Harvest draws with two microcodes, F3DEX 1.21 and L3DEX 1.21 (the line
variant). RT64's GBI database recognises "L3DEX 1.21" but registers it -- and
L3DEX 1.00 / 1.23 / 1.23 (Variant) -- as `GBIUCode::Unknown`, which the setup
switch has no case for (`assert(false)`, compiled out). Only the common RDP
handlers get installed, so G_VTX, G_DL, G_ENDDL, G_MTX have no handler: the
interpreter walks straight through the list and on through whatever follows it,
executing vertex and texture data as commands until one looks like a TLUT load
with a wild address.

L3DEX 1.x is F3DEX 1.x with a line primitive; the opcode numbering is F3DEX's.
Routing the four entries to `GBIUCode::F3DEX` gives them every handler they
share. What F3DEX lacks is the line command (0xB5 in L3DEX, F3DEX's quad slot):
lines, if the game draws any through it, come out as degenerate quads --
invisible, not garbage. A real line handler is future work if something is seen
to be missing.

Finding credited to 12feihu's BodyHarvest-PC-Port (docs/HANDOFF.md, "The Phase 12c
fix"), re-derived here from this port's crash and RT64's table.

Scripted and idempotent because it patches a submodule. Run from the repository
root:
    python tools/patch_rt64_l3dex.py
"""

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "lib" / "RT64" / "src" / "gbi" / "rt64_gbi.cpp"

NAMES = ["L3DEX 1.00", "L3DEX 1.21", "L3DEX 1.23", "L3DEX 1.23 (Variant)"]


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    changed = 0
    for name in NAMES:
        quoted = f'"{name}",'
        lines = text.splitlines(keepends=True)
        hits = [i for i, l in enumerate(lines) if quoted in l and "GBIInstance" in l]
        if len(hits) != 1:
            print(f"  anchor for {name!r} found {len(hits)} times -- RT64 changed, re-check the table", file=sys.stderr)
            return 1
        line = lines[hits[0]]
        if "GBIUCode::F3DEX," in line:
            continue
        if "GBIUCode::Unknown," not in line:
            print(f"  {name!r} is neither Unknown nor F3DEX: {line.strip()}", file=sys.stderr)
            return 1
        lines[hits[0]] = line.replace("GBIUCode::Unknown,", "GBIUCode::F3DEX,  ", 1).rstrip("\n") \
            + " // bh: tools/patch_rt64_l3dex.py\n"
        text = "".join(lines)
        changed += 1
    if changed:
        TARGET.write_text(text, encoding="utf-8", newline="")
        print(f"  rt64_gbi.cpp: {changed} L3DEX 1.x entries routed to F3DEX")
    else:
        print("  rt64_gbi.cpp: L3DEX 1.x already routed to F3DEX")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
