# Phase 02 findings — first recompile

**Gate:** met. N64Recomp and RSPRecomp run under WSL, the function count reconciles with the ELF, and
all 30 generated files plus the recompiled microcode compile into `bh_recompiled.lib`.

Reproduce with:

```
wsl -d Ubuntu -e bash tools/regenerate.sh          # or tools/recompile.sh with an existing ELF
cmake -B build -DBH_WITH_RECOMPILED=ON && cmake --build build --target bh_recompiled
```

## Answer

One function needed a config entry (the split-off tail of `__osException`, listed in `ignored`); nothing
else in 2,915 recompiled functions was refused. The eight overlays go in as sections without relocation
data and N64Recomp accepts them at shared vrams. The audio microcode is the stock `aspMain` that
Pilotwings 64 and Wave Race 64 already recompile.

## Measurements

| What | How | Result |
|---|---|---|
| N64Recomp / RSPRecomp build | `tools/wsl_build_recompiler.sh` | both link (N64Recomp `81213c1` + `patch_n64recomp.py`, RSPRecomp + `patch_rsprecomp.py`) |
| First run | `tools/recompile.sh` | `Unhandled branch in func_800235E4_241E4 at 0x800236C4 to 0x8002359C` — branches into `__osException` |
| After `ignored = ["func_800235E4_241E4"]` | `tools/recompile.sh` | 0 errors, 0 warnings; `funcs.h` ends in `#endif` |
| Function reconciliation | `tools/count_recompiled.py` | 3,041 FUNC in ELF − 125 runtime-owned by name − 1 ignored by config = **2,915 emitted**, 30 source files |
| Sections in `recomp_overlays.inl` | file | `num_sections = 40`; 8 overlay indices |
| Runtime-provided libultra | `tools/gen_runtime_func_table.py` | 143 `_recomp` definitions; 67 have a cartridge address and are registered; 76 have none in this ELF (compiler helpers, other hardware checks) |
| Declarations | `tools/gen_reimplemented_decls.py` | 440 (116 reimplemented, 384 ignored, 83 renamed, 1 ours) |
| `aspMain` identity | Python compare of ROM bytes | text at ROM `0x2FF10`, `0xE20` bytes, SHA-1 `21e05bb9…`, identical to Pilotwings 64's at `0x48E10`; command table at `0x3FC70` = `0x1118, 0x1470, …, 0x144C`, identical |
| `aspMain` extent | bytes at `0x2FF10 + 0xE20` | `201d0110 34022800 …` — the start of the next microcode, so `0xE20` is the real end; the declared `0xF80` would overrun |
| Static library | `cmake --build build --target bh_recompiled` (clang-cl, RelWithDebInfo) | 33 objects, `bh_recompiled.lib` 10.8 MB, 10 s |

## Negative results

- **Third-party port's `text_size = 0xF80`** — refuted by the byte comparison above; it is the task's
  declared size, not the microcode's (the Rayman 2 trap in playbook 06).

## What is not established

- Whether any of the eight overlays' functions call a resident function through a pointer the section
  table misses — phase 04 lookup-miss reports.
- `func_800235E4_241E4` is assumed unreachable (the runtime owns exceptions). A lookup miss at
  `0x800235E4` would refute that.

## Consequence for the plan

Phase 03 links the harness against `bh_recompiled`.
