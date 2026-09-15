#!/usr/bin/env python3
"""Verify the ELF N64Recomp reads against the dump. Stdlib only.

    python3 tools/verify_elf.py elf/bh.us.fixed.elf rom.z64 elf/bh.us.yaml

Checks (playbook 02, "the numbers that must be zero"):

- every allocated PROGBITS section's bytes equal the dump at its ROM address,
  where the ROM address is what N64Recomp computes: the containing PT_LOAD
  segment's physical address plus the section's offset into it;
- every splat segment of type code in the yaml starts at the ROM address and vram
  of the ELF section with its name;
- every function whose name encodes an address (`func_80070270_7F220`,
  `func_80070270`) sits at that vram, and at that ROM offset where the name
  carries one. Data labels (`D_...`) are checked the same way but only listed:
  once the bytes are identical, a data label that disagrees with its own name is
  a naming slip in the decomp (31 at the pinned revision), not a placement error;
- no FUNC symbol is ABS or has size 0, and no two functions in a section overlap.

Exits non-zero on any mismatch.
"""

import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0].rsplit("\\", 1)[0])
from fix_elf import Elf, SHN_ABS, STT_FUNC, SHF_EXECINSTR  # noqa: E402

PT_LOAD = 1
SHT_PROGBITS = 1
SHF_ALLOC = 0x2
NAME_RE = re.compile(r"^(?:func|D|jtbl|L)_([0-9A-F]{8})(?:_([0-9A-F]+))?$")


def load_segments(data):
    phoff, = struct.unpack_from(">I", data, 0x1C)
    phentsize, phnum = struct.unpack_from(">HH", data, 0x2A)
    segs = []
    for i in range(phnum):
        typ, offset, vaddr, paddr, filesz, memsz, flags, align = \
            struct.unpack_from(">IIIIIIII", data, phoff + i * phentsize)
        if typ == PT_LOAD:
            segs.append((offset, paddr, filesz))
    return segs


def yaml_code_segments(path):
    """name -> (start, vram) for top-level `type: code` segments. Line-based: the
    decomp's yaml keeps name/type/start/vram on their own lines."""
    out, cur = {}, None
    for line in open(path, encoding="utf-8"):
        m = re.match(r"^  - name: (\S+)", line)
        if m:
            cur = {"name": m.group(1)}
            continue
        if cur is None:
            continue
        m = re.match(r"^    (type|start|vram): (\S+)", line)
        if m:
            cur[m.group(1)] = m.group(2)
            if cur.get("type") == "code" and "start" in cur and "vram" in cur:
                out[cur["name"]] = (int(cur["start"], 16), int(cur["vram"], 16))
    return out


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    data = bytearray(open(sys.argv[1], "rb").read())
    rom = open(sys.argv[2], "rb").read()
    elf = Elf(data)
    segs = load_segments(data)

    section_rom = {}
    checked = wrong_size = wrong_bytes = 0
    for i, s in enumerate(elf.sections):
        if s["type"] != SHT_PROGBITS or not s["flags"] & SHF_ALLOC or s["size"] == 0:
            continue
        seg = next(((o, p, f) for (o, p, f) in segs if o <= s["offset"] < o + f), None)
        if seg is None:
            print(f"  {s['name']}: in no PT_LOAD segment")
            wrong_size += 1
            continue
        r = seg[1] + (s["offset"] - seg[0])
        section_rom[i] = r
        checked += 1
        body = data[s["offset"]:s["offset"] + s["size"]]
        if r + s["size"] > len(rom):
            print(f"  {s['name']}: runs past the dump (rom 0x{r:X} + 0x{s['size']:X})")
            wrong_size += 1
            continue
        if body != rom[r:r + s["size"]]:
            diff = sum(1 for a, b in zip(body, rom[r:r + s["size"]]) if a != b)
            print(f"  {s['name']}: {diff} bytes differ (rom 0x{r:X})")
            wrong_bytes += diff
    print(f"sections checked : {checked}     wrong size : {wrong_size}     wrong bytes : {wrong_bytes}")

    ymap = yaml_code_segments(sys.argv[3])
    by_name = {s["name"]: (i, s) for i, s in enumerate(elf.sections)}
    seg_bad = 0
    for name, (start, vram) in ymap.items():
        hit = by_name.get("." + name)
        if hit is None:
            print(f"  yaml segment {name}: no ELF section")
            seg_bad += 1
            continue
        i, s = hit
        if section_rom.get(i) != start or s["addr"] != vram:
            print(f"  {name}: yaml rom 0x{start:X} vram 0x{vram:08X}, ELF rom 0x{section_rom.get(i, -1):X} "
                  f"vram 0x{s['addr']:08X}")
            seg_bad += 1
    print(f"code segments    : {len(ymap)} in yaml, {seg_bad} disagree with the ELF")

    placed = misplaced = 0
    data_misnamed = []
    funcs = defaultdict(list)
    abs_funcs = zero = 0
    for sym in elf.symbols():
        if sym["type"] == STT_FUNC:
            if sym["shndx"] == SHN_ABS:
                abs_funcs += 1
            elif sym["size"] == 0:
                zero += 1
            else:
                funcs[sym["shndx"]].append(sym)
        m = NAME_RE.match(sym["name"])
        if not m or sym["shndx"] not in section_rom:
            continue
        vram = int(m.group(1), 16)
        ok = sym["value"] == vram
        if m.group(2) is not None:
            sec = elf.sections[sym["shndx"]]
            ok = ok and section_rom[sym["shndx"]] + (sym["value"] - sec["addr"]) == int(m.group(2), 16)
        if ok:
            placed += 1
        elif sym["name"].startswith("func_"):
            misplaced += 1
            if misplaced <= 10:
                print(f"  misplaced function: {sym['name']} at 0x{sym['value']:08X}")
        else:
            data_misnamed.append(sym)
    print(f"symbols placed   : {placed}/{placed + misplaced + len(data_misnamed)} exact; "
          f"{misplaced} function names disagree")
    # Data labels whose names disagree with their address are the decomp's naming,
    # not placement: the section bytes are already proven identical above. They
    # are listed so a patch never trusts such a name for an address.
    for sym in data_misnamed:
        print(f"  note: data label {sym['name']} sits at 0x{sym['value']:08X}")

    overlaps = 0
    total = 0
    for shndx, lst in funcs.items():
        if not elf.sections[shndx]["flags"] & SHF_EXECINSTR:
            continue
        lst.sort(key=lambda s: s["value"])
        total += len(lst)
        for a, b in zip(lst, lst[1:]):
            if a["value"] + a["size"] > b["value"]:
                overlaps += 1
                if overlaps <= 10:
                    print(f"  overlap: {a['name']} (+0x{a['size']:X}) runs into {b['name']}")
    print(f"functions        : {total}     ABS : {abs_funcs}     size 0 : {zero}     overlaps : {overlaps}")

    bad = wrong_size + wrong_bytes + seg_bad + misplaced + abs_funcs + zero + overlaps
    print("ELF verified" if bad == 0 else "ELF NOT verified")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
