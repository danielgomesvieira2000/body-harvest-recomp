#!/usr/bin/env python3
"""Make the decompilation's ELF safe for N64Recomp. Stdlib only.

    python3 tools/fix_elf.py elf/bh.us.elf elf/bh.us.fixed.elf

The decomp's build is byte-exact, but two things in its symbol table are traps
that byte identity cannot see (playbook 02):

1. **Zero-size FUNC symbols.** Hand-written assembly (`glabel` in the decomp's
   macro.inc emits `.type @function` but no `.size`) and a few empty static C
   functions come out with st_size 0. N64Recomp derives a function's words from
   its size, so a zero-size function is silently not recompiled (Rayman 2
   emitted one function out of 4,465 this way). Each one gets the distance to
   the next FUNC or OBJECT symbol in its section, or to the section's end.
   NOTYPE symbols are not boundaries: in this ELF they are jump-table and local
   labels that sit *inside* functions.

2. **FUNC symbols in SHN_ABS.** The decomp's undefined_syms file assigns
   addresses to names referenced across overlays (`func_80070270 = 0x80070270;`).
   A linker assignment detaches the symbol from any section, and N64Recomp
   resolves functions by section. The real function is always also present under
   its section-bound `$VRAM_$ROM` name; the ABS alias is demoted to NOTYPE so it
   cannot be mistaken for a function. Where the assignment names the function
   itself (`func_8011D260_12C210 = 0x8011D260;` shadows the definition, so no
   section-bound twin exists), the symbol is rebound to the code section whose
   ROM range holds the offset in its name (or the only code section holding its
   vram). One that fits neither is an error (the function would be missing).

Also reported and resolved: two FUNC symbols at one address in one section.
The one with a non-zero size (else the global one, else the first) stays FUNC;
the others become NOTYPE, so N64Recomp emits the function once.

Every count is printed, and the script exits non-zero if the end state is not
clean (no size-0 FUNC, no ABS FUNC, no duplicate FUNC addresses).
"""

import re
import struct
import sys
from collections import defaultdict

SHT_SYMTAB = 2
SHT_NOBITS = 8
SHF_EXECINSTR = 0x4
SHN_ABS = 0xFFF1
STT_NOTYPE, STT_OBJECT, STT_FUNC = 0, 1, 2


class Elf:
    def __init__(self, data: bytearray):
        self.data = data
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            raise SystemExit("not a 32-bit big-endian ELF")
        (self.shoff,) = struct.unpack_from(">I", data, 0x20)
        self.shentsize, self.shnum, self.shstrndx = struct.unpack_from(">HHH", data, 0x2E)
        self.sections = []
        for i in range(self.shnum):
            off = self.shoff + i * self.shentsize
            name, typ, flags, addr, offset, size, link, info, align, entsize = \
                struct.unpack_from(">IIIIIIIIII", data, off)
            self.sections.append(dict(name_off=name, type=typ, flags=flags, addr=addr,
                                      offset=offset, size=size, link=link, entsize=entsize))
        strtab = self.sections[self.shstrndx]
        for s in self.sections:
            s["name"] = self._cstr(strtab["offset"] + s["name_off"])

    def _cstr(self, off: int) -> str:
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("ascii", "replace")

    def symbols(self):
        symtab = next(s for s in self.sections if s["type"] == SHT_SYMTAB)
        strtab = self.sections[symtab["link"]]
        for i in range(symtab["size"] // symtab["entsize"]):
            off = symtab["offset"] + i * symtab["entsize"]
            name, value, size, info, other, shndx = struct.unpack_from(">IIIBBH", self.data, off)
            yield dict(index=i, off=off, name=self._cstr(strtab["offset"] + name), value=value,
                       size=size, bind=info >> 4, type=info & 0xF, shndx=shndx)

    def set_size(self, sym, size):
        struct.pack_into(">I", self.data, sym["off"] + 8, size)

    def set_shndx(self, sym, shndx):
        struct.pack_into(">H", self.data, sym["off"] + 14, shndx)

    def section_rom(self):
        """section index -> ROM address, as N64Recomp computes it: the containing
        PT_LOAD segment's physical address plus the offset into it."""
        phoff, = struct.unpack_from(">I", self.data, 0x1C)
        phentsize, phnum = struct.unpack_from(">HH", self.data, 0x2A)
        segs = []
        for i in range(phnum):
            typ, offset, vaddr, paddr, filesz = struct.unpack_from(">IIIII", self.data, phoff + i * phentsize)
            if typ == 1:
                segs.append((offset, paddr, filesz))
        out = {}
        for i, s in enumerate(self.sections):
            for (o, p, f) in segs:
                if s["type"] != SHT_NOBITS and s["size"] and o <= s["offset"] < o + f:
                    out[i] = p + (s["offset"] - o)
        return out

    def set_type(self, sym, typ):
        info = (sym["bind"] << 4) | typ
        struct.pack_into(">B", self.data, sym["off"] + 12, info)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    elf = Elf(bytearray(open(sys.argv[1], "rb").read()))
    syms = list(elf.symbols())

    code = {i for i, s in enumerate(elf.sections)
            if s["flags"] & SHF_EXECINSTR and s["type"] != SHT_NOBITS and s["size"] > 0}

    funcs = [s for s in syms if s["type"] == STT_FUNC]
    abs_funcs = [s for s in funcs if s["shndx"] == SHN_ABS]
    sec_funcs = [s for s in funcs if s["shndx"] in code]
    other_funcs = [s for s in funcs if s["shndx"] not in code and s["shndx"] != SHN_ABS]
    print(f"FUNC symbols: {len(funcs)} ({len(sec_funcs)} in code sections, {len(abs_funcs)} ABS, "
          f"{len(other_funcs)} elsewhere)")
    if other_funcs:
        for s in other_funcs:
            print(f"  FUNC outside a code section: {s['name']} shndx {s['shndx']}")
        return 1

    # 1. ABS aliases.
    by_addr = defaultdict(list)
    for s in sec_funcs:
        by_addr[s["value"]].append(s)
    missing = 0
    rebound = []
    sec_rom = elf.section_rom()
    for s in abs_funcs:
        twins = by_addr.get(s["value"], [])
        where = ", ".join(f"{t['name']}@{elf.sections[t['shndx']]['name']}" for t in twins)
        if twins:
            print(f"  ABS {s['name']} -> NOTYPE (real: {where})")
            elf.set_type(s, STT_NOTYPE)
            continue
        m = re.match(r"^func_([0-9A-F]{8})_([0-9A-F]+)$", s["name"])
        homes = [i for i in code
                 if elf.sections[i]["addr"] <= s["value"] < elf.sections[i]["addr"] + elf.sections[i]["size"]]
        if m:
            rom = int(m.group(2), 16)
            homes = [i for i in homes if sec_rom.get(i) is not None
                     and sec_rom[i] + (s["value"] - elf.sections[i]["addr"]) == rom]
        if len(homes) == 1:
            elf.set_shndx(s, homes[0])
            s["shndx"] = homes[0]
            rebound.append(s)
            print(f"  ABS {s['name']} -> rebound to {elf.sections[homes[0]]['name']} (assignment shadowed the definition)")
        else:
            print(f"  ERROR: ABS {s['name']} 0x{s['value']:08X} has no section-bound FUNC and {len(homes)} candidate sections")
            missing += 1
            elf.set_type(s, STT_NOTYPE)
    sec_funcs.extend(rebound)

    # 2. Duplicates within one section.
    per_sec_addr = defaultdict(list)
    for s in sec_funcs:
        per_sec_addr[(s["shndx"], s["value"])].append(s)
    demoted = set()
    for (shndx, addr), group in per_sec_addr.items():
        if len(group) < 2:
            continue
        keep = sorted(group, key=lambda s: (s["size"] == 0, s["bind"] != 1))[0]
        for s in group:
            if s is not keep:
                elf.set_type(s, STT_NOTYPE)
                demoted.add(s["index"])
        print(f"  duplicate at 0x{addr:08X} in {elf.sections[shndx]['name']}: kept {keep['name']}, "
              f"demoted {', '.join(s['name'] for s in group if s is not keep)}")
    live = [s for s in sec_funcs if s["index"] not in demoted]

    # 3. Sizes. Boundaries: every FUNC and OBJECT start in the section.
    bounds = defaultdict(set)
    for s in syms:
        if s["shndx"] in code and s["type"] in (STT_FUNC, STT_OBJECT) and s["index"] not in demoted:
            bounds[s["shndx"]].add(s["value"])
    sorted_bounds = {k: sorted(v) for k, v in bounds.items()}
    fixed = 0
    for s in live:
        if s["size"] != 0:
            continue
        sec = elf.sections[s["shndx"]]
        end = sec["addr"] + sec["size"]
        nxt = next((b for b in sorted_bounds[s["shndx"]] if b > s["value"]), end)
        size = nxt - s["value"]
        if size <= 0 or size % 4 or size > 0x40000:
            print(f"  ERROR: {s['name']} computed size 0x{size:X}")
            return 1
        elf.set_size(s, size)
        fixed += 1
    print(f"sized {fixed} zero-size FUNC symbols; handled {len(abs_funcs)} ABS ({len(rebound)} rebound) and {len(demoted)} duplicates")

    # End state, re-read from the patched bytes.
    after = [s for s in Elf(elf.data).symbols() if s["type"] == STT_FUNC]
    bad_abs = sum(1 for s in after if s["shndx"] == SHN_ABS)
    bad_zero = sum(1 for s in after if s["size"] == 0)
    dup = len(after) - len({(s["shndx"], s["value"]) for s in after})
    per_section = defaultdict(int)
    for s in after:
        per_section[elf.sections[s["shndx"]]["name"]] += 1
    for name, n in per_section.items():
        print(f"  {name:40s} {n:5d} functions")
    print(f"end state: {len(after)} FUNC, {bad_abs} ABS, {bad_zero} size 0, {dup} duplicate addresses")
    if bad_abs or bad_zero or dup or missing:
        return 1
    open(sys.argv[2], "wb").write(elf.data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
