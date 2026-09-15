#!/usr/bin/env python3
"""Reconcile the recompiler's output with the ELF (playbook 03: every stage counts).

functions emitted must equal FUNC symbols in the ELF minus those N64Recomp hands
to the runtime by name (reimplemented or ignored). A shortfall means functions
were silently dropped (Rayman 2's 1-of-4,465 failure); exits non-zero.

Usage (inside WSL):  python3 tools/count_recompiled.py
"""
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LISTS = REPO / "lib/N64ModernRuntime/N64Recomp/src/symbol_lists.cpp"
ELF = REPO / "elf/body-harvest.us.elf"


def names(src, marker):
    s = src.index(marker)
    return set(re.findall(r'"([^"]+)"', src[s:src.index("};", s)]))


def main() -> int:
    src = LISTS.read_text()
    runtime = names(src, "reimplemented_funcs {") | names(src, "ignored_funcs {")
    out = subprocess.run(["mips-linux-gnu-readelf", "-sW", str(ELF)], capture_output=True, text=True, check=True).stdout
    funcs = [p[7] for p in (l.split() for l in out.splitlines())
             if len(p) >= 8 and p[3] == "FUNC" and p[6] not in ("UND", "ABS")]
    owned = sum(1 for n in funcs if n in runtime)
    emitted = sum(len(re.findall(r"^RECOMP_FUNC void ", f.read_text(), re.M))
                  for f in (REPO / "RecompiledFuncs").glob("funcs_*.c"))
    expected = len(funcs) - owned
    print("FUNC symbols in ELF  : %d" % len(funcs))
    print("owned by the runtime : %d (reimplemented or ignored by name)" % owned)
    print("functions emitted    : %d (expected %d)" % (emitted, expected))
    print("source files         : %d" % len(list((REPO / "RecompiledFuncs").glob("funcs_*.c"))))
    if emitted != expected:
        print("MISMATCH: functions were dropped or duplicated", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
