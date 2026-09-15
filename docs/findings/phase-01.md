# Phase 01 findings — ROM to ELF

**Gate:** met. The decomp's own build matches the dump (`build/bh.us.z64: OK`), and the
post-processed ELF verifies with every count at zero.

Reproduce with:

```
wsl -d Ubuntu -e bash tools/wsl_build_elf.sh            # mirror, make extract, make, fix, verify
python3 tools/fix_elf.py elf/bh.us.elf elf/bh.us.fixed.elf
python3 tools/verify_elf.py elf/bh.us.fixed.elf rom.z64 elf/bh.us.yaml
```

## Answer

Route A works as planned: the pinned decomp (`46006777`) builds a byte-identical ROM in a WSL mirror
in seconds, and its ELF needs three symbol-table repairs before N64Recomp can read it — sizes for 173
hand-written functions, six linker-assignment ABS FUNC symbols (two of which shadow the only
definition of a real function), and two alias groups at one address. None needs a change to the decomp.

## Measurements

| What | How | Result |
|---|---|---|
| Decomp build | `tools/wsl_build_elf.sh` (make extract + make -j12 in `~/.cache/body-harvest-recomp/decomp`) | `build/bh.us.z64: OK` |
| FUNC symbols as built | `fix_elf.py` | 3,049: 3,043 in code sections, 6 ABS |
| Zero-size FUNC | `fix_elf.py` | 173 sized to the next FUNC/OBJECT start in the section (8 more were duplicates/ABS) |
| ABS FUNC with a section-bound twin | `fix_elf.py` | 4 demoted to NOTYPE: `func_80070270` (3 twins, one per gameplay overlay), `func_80002AB4` (`getSaveFileName`), `func_8000A160`, `func_8001BCE0_1C8E0` (`osAiGetLength`) |
| ABS FUNC shadowing its own definition | `fix_elf.py` | 2 rebound to `.overlay_gameplay_outside` by the ROM offset in the name: `func_8011D260_12C210`, `func_80128504_1374B4` (decomp `undefined_syms.us.txt:6370,6376` assigns the full names) |
| Duplicate FUNC addresses | `fix_elf.py` | `_bcopy`/`bcopy` at `0x8002C3B0`; `_bzero`/`_blkclr`/`bzero`/`blkclr` at `0x8001E920` — kept `_bcopy`, `_bzero` |
| Functions after fixing | `fix_elf.py` | 3,041: core 683, frontend 218, outside 1,446, inside 237, greece 64, java 102, america 74, siberia 95, comet 122 |
| Section bytes vs dump | `verify_elf.py` (ROM address = PT_LOAD paddr + offset, as N64Recomp computes it) | 27 sections, 0 wrong size, 0 wrong bytes |
| yaml code segments vs ELF | `verify_elf.py` | 14 segments, 0 disagree |
| Address-named symbols | `verify_elf.py` | 5,531 / 5,562 exact; 0 function names disagree; 31 data labels (e.g. `D_8013D778_14C730` at `0x8013D780`) disagree with their own names |
| Overlapping functions | `verify_elf.py` | 0 |
| Program headers | `mips-linux-gnu-readelf -lW` | 12 PT_LOAD; physical address = ROM offset for every segment (`.core` paddr `0x1000`) — N64Recomp's `rom_addr` comes out right without a linker-script change |

## Negative results

- **"Data label names encode their address, so check them strictly"** (the WR64/RAY2 drift check) —
  31 decomp data labels disagree with their own names while every byte matches. With byte identity
  already proven, strict checking would fail a correct ELF; the verifier lists them instead so no patch
  trusts such a name for an address.

## Inference

- *Inferred:* sizing a hand-written function to the next FUNC/OBJECT start can include trailing
  alignment padding (`nop`s). Harmless for recompilation; phase 02 would report an invalid instruction
  if data were swallowed instead.

## What is not established

- Whether N64Recomp accepts overlapping non-relocatable sections at one vram without `.rel` data — phase 02.

## Consequence for the plan

Phase 02 reads `elf/bh.us.fixed.elf`.
