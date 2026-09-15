#!/usr/bin/env python3
"""Callers and callees of a function, read from splat's asm/ output.

For naming functions from evidence (playbook 03): which already-proven functions
a candidate calls, and which call it. Also prints the hardware-register pages and
cop0/cop1 register numbers the body touches, which identify libultra routines.

Usage:
    python tools/callgraph.py <name-or-0xADDR> [...]       callers, callees, registers
    python tools/callgraph.py --dis <name-or-0xADDR>       also print the body
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ASM = REPO / "asm"

GLABEL = re.compile(r"^glabel (\S+)")
JAL = re.compile(r"\bjal\s+(\S+)")
LINE = re.compile(r"/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/\s+(.*)")


def load():
    bodies, callees, callers, addr_of = {}, defaultdict(list), defaultdict(list), {}
    for path in sorted(ASM.glob("*.s")):
        cur = None
        for line in path.read_text().splitlines():
            m = GLABEL.match(line)
            if m:
                cur = m.group(1)
                bodies[cur] = []
                continue
            if cur is None:
                continue
            lm = LINE.search(line)
            if lm:
                if not bodies[cur]:
                    addr_of[cur] = int(lm.group(1), 16)
                bodies[cur].append((int(lm.group(1), 16), int(lm.group(2), 16), lm.group(3)))
                jm = JAL.search(lm.group(3))
                if jm:
                    callees[cur].append(jm.group(1))
                    callers[jm.group(1)].append(cur)
    return bodies, callees, callers, addr_of


def resolve(key, bodies, addr_of):
    if key.lower().startswith("0x"):
        want = int(key, 16)
        return [n for n, a in addr_of.items() if a == want]
    return [key] if key in bodies else []


def registers(body):
    pages, cop = set(), set()
    for vram, word, text in body:
        op = word >> 26
        if op == 0x0F and 0xA000 <= (word & 0xFFFF) < 0xC000:
            pages.add("0x%04Xxxxx" % (word & 0xFFFF))
        if op in (0x10, 0x11) and ((word >> 21) & 31) in (0, 2, 4, 6):
            cop.add("cop%d %s r%d" % (op & 3, {0: "mfc", 2: "cfc", 4: "mtc", 6: "ctc"}[(word >> 21) & 31],
                                      (word >> 11) & 31))
    return sorted(pages), sorted(cop)


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--dis"]
    dis = "--dis" in sys.argv
    if not args:
        print(__doc__)
        return 1
    bodies, callees, callers, addr_of = load()
    for key in args:
        for name in resolve(key, bodies, addr_of) or [key]:
            if name not in bodies:
                print("%s: not found" % key)
                continue
            pages, cop = registers(bodies[name])
            print("== %s @ 0x%08X, %d words" % (name, addr_of[name], len(bodies[name])))
            print("   callees: %s" % (", ".join(dict.fromkeys(callees[name])) or "-"))
            print("   callers: %s" % (", ".join(dict.fromkeys(callers[name])) or "-"))
            if pages or cop:
                print("   hw pages: %s   %s" % (" ".join(pages), " ".join(cop)))
            if dis:
                for vram, word, text in bodies[name]:
                    print("     %08X %08X  %s" % (vram, word, text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
