#!/usr/bin/env python3
"""Survey an N64 dump for the facts a port's build plan rests on.

The phase 00 measurements (recorded in Rayman 2's docs/PHASE00-FINDINGS.md):
the graphics microcode, the save medium, the extent of the flat code image,
and a lower bound on the function count from a JAL-target scan.

Usage:  python tools/survey_rom.py path/to/rom.z64
"""
import collections
import math
import re
import struct
import sys

BASE_VRAM = 0x80000400   # cartridge header entry point
BASE_ROM = 0x1000        # first byte after the header + IPL3 boot block


def plausible_mips(w: int) -> bool:
    """True if a big-endian word decodes to a commonly-emitted MIPS III instruction."""
    if w == 0:
        return True                                     # nop
    op = w >> 26
    if op == 0:                                         # SPECIAL
        return (w & 0x3F) in {
            0x00, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x09, 0x0C, 0x0D,
            0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B, 0x20, 0x21,
            0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B,
        }
    return op in {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x14, 0x15, 0x16, 0x17, 0x20,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E,
        0x30, 0x31, 0x35, 0x38, 0x39, 0x3D,
    }


def entropy(buf: bytes) -> float:
    if not buf:
        return 0.0
    counts = collections.Counter(buf)
    n = len(buf)
    return -sum((c / n) * math.log2(c / n) for c in counts.values())


def survey(data: bytes) -> None:
    print("== graphics microcode ==")
    ucodes = [m.group().decode() for m in re.finditer(rb"RSP [ -~]{10,70}", data)]
    for u in ucodes:
        print(f"  {' '.join(u.split())}")
    print(f"  ({len(ucodes)} microcode identifier string(s) in the ROM)")

    print("\n== save medium ==")
    for pat, label in ((rb"Controller Pak", "Controller Pak"), (rb"osPfs", "libultra Pak API")):
        hits = len(re.findall(pat, data))
        if hits:
            print(f"  {label}: {hits} reference(s)")

    print("\n== code image extent (4 KB blocks, >90% plausible MIPS + >=4 calls) ==")
    BLK = 0x1000
    runs, cur = [], None
    for off in range(BASE_ROM, len(data) - BLK, BLK):
        words = struct.unpack(f">{BLK // 4}I", data[off:off + BLK])
        dens = sum(1 for w in words if plausible_mips(w)) / len(words)
        calls = sum(1 for w in words if (w >> 26) == 3 or w == 0x03E00008)
        is_code = dens > 0.90 and calls >= 4
        if is_code and cur is None:
            cur = off
        elif not is_code and cur is not None:
            runs.append((cur, off))
            cur = None
    if cur is not None:
        runs.append((cur, len(data)))

    total = 0
    for a, b in runs:
        if b - a >= 0x2000:
            print(f"  0x{a:07X} - 0x{b:07X}   {(b - a) / 1024:7.1f} KB   "
                  f"(vram 0x{BASE_VRAM + a - BASE_ROM:08X})   entropy {entropy(data[a:b]):.2f}")
            total += b - a
    print(f"  total: {total / 1024:.1f} KB")

    print("\n== call-target scan over the flat code image ==")
    end_rom = max((b for a, b in runs if a < 0x200000), default=0xD0000)
    seg = data[BASE_ROM:end_rom]
    words = struct.unpack(f">{len(seg) // 4}I", seg)
    end_vram = BASE_VRAM + len(seg)
    targets = collections.Counter()
    returns = 0
    for i, w in enumerate(words):
        if (w >> 26) == 3:                              # jal
            pc = BASE_VRAM + i * 4
            targets[(pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)] += 1
        if w == 0x03E00008:                             # jr $ra
            returns += 1
    inside = {t: c for t, c in targets.items() if BASE_VRAM <= t < end_vram}
    print(f"  window            0x{BASE_ROM:X}-0x{end_rom:X}  "
          f"(vram 0x{BASE_VRAM:08X}-0x{end_vram:08X})")
    print(f"  jal instructions  {sum(targets.values())}")
    print(f"  unique targets    {len(targets)}  ({len(inside)} inside the window)")
    print(f"  in-window share   {100 * sum(inside.values()) / max(sum(targets.values()), 1):.1f}%")
    print(f"  'jr $ra' count    {returns}")
    print(f"\n  => at least {len(inside)} distinct functions need symbols.")

    print("\n== retained source-file names (assert strings) ==")
    names = sorted({m.group().decode() for m in
                    re.finditer(rb"[A-Za-z0-9_./]{3,40}\.[ch]\x00", data)})
    for n in names[:24]:
        print(f"  {n.rstrip(chr(0))}")
    if len(names) > 24:
        print(f"  ... and {len(names) - 24} more")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} path/to/rom.z64")
    data = open(sys.argv[1], "rb").read()
    if data[:4] != b"\x80\x37\x12\x40":
        raise SystemExit("error: expected a big-endian (.z64) dump; "
                         "run tools/identify_rom.py to check the format")
    survey(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
