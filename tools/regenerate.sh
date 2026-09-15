#!/usr/bin/env bash
# Everything from the dump to RecompiledFuncs/, in order, each stage checking its
# own end state (playbook 03: regeneration is a pipeline, not a command).
#
#   wsl -d Ubuntu -e bash tools/regenerate.sh [rom.z64]
#
# 1. tools/wsl_build_recompiler.sh  N64Recomp + RSPRecomp (only if not built)
# 2. tools/wsl_build_elf.sh         decomp build (must match), fix_elf, verify_elf
# 3. tools/recompile.sh             N64Recomp, context, declarations, runtime
#                                   function table, RSPRecomp, counts
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO/lib/N64ModernRuntime/N64Recomp/build-linux"
if [ ! -x "$BIN/N64Recomp" ] || [ ! -x "$BIN/RSPRecomp" ]; then
    bash "$REPO/tools/wsl_build_recompiler.sh"
fi
bash "$REPO/tools/wsl_build_elf.sh" "${1:-$REPO/rom.z64}"
bash "$REPO/tools/recompile.sh"
