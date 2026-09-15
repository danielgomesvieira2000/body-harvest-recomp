"""Apply every submodule patch this port needs, in order. Idempotent.

Each patch is its own script, with the reason for it at the top; this only runs
them. A submodule update reverts a hand edit silently, which is why none of
these changes are made by hand -- rerun this after any submodule update.

    python tools/patch_all.py
"""

import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent

# ---- port config: the patch scripts this port applies, in order ------------
# Scripts that are not present in tools/ are skipped.
SCRIPTS = [
    "patch_rsprecomp.py",          # RSPRecomp: indirect jumps ignore the low two bits
    "patch_n64recomp.py",          # N64Recomp: use_lookup_for_all_function_calls config key
    "patch_librecomp.py",          # librecomp: report the caller on a failed function lookup
    "patch_runtime_shutdown.py",   # librecomp/ultramodern: join workers before freeing RDRAM
    "patch_recompinput.py",        # recompinput: auto-assign controllers to players
    "patch_rt64_eventfilter.py",   # RT64: take the SDL event filter back off
    "patch_rt64_inspector.py",     # RT64: port hook in the F1 developer UI; F2 unbound
    "patch_rt64_texturepacks.py",  # RT64: texture packs from mods (after the inspector patch)
    "patch_rt64_pairing.py",       # RT64: count interpolation pairing (RT64_GetTransformPairing)
]
# ----------------------------------------------------------------------------


def main() -> int:
    failed = []
    for script in SCRIPTS:
        path = TOOLS / script
        if not path.is_file():
            continue
        print(f"--- {script}")
        if subprocess.run([sys.executable, str(path)]).returncode != 0:
            failed.append(script)
    if failed:
        print(f"\nfailed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nall submodule patches are in place")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
