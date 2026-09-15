#!/usr/bin/env bash
# Phase 02: run N64Recomp and RSPRecomp over the ELF and the dump, and check the
# result is complete. Regeneration is a pipeline, not a command (playbook 03):
# recompiler -> reference context -> declarations -> runtime function table ->
# RSPRecomp, with counts at every stage.
#
# Runs under Linux/WSL: the Windows N64Recomp has died mid-write (0xC0000409).
# Needs tools/wsl_build_recompiler.sh and tools/wsl_build_elf.sh first.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO/lib/N64ModernRuntime/N64Recomp/build-linux"
ELF="$REPO/elf/bh.us.fixed.elf"

for tool in N64Recomp RSPRecomp; do
    [ -x "$BIN/$tool" ] || { echo "$tool is not built -- run tools/wsl_build_recompiler.sh" >&2; exit 1; }
done
[ -f "$ELF" ] || { echo "elf/bh.us.fixed.elf missing -- run tools/wsl_build_elf.sh" >&2; exit 1; }

cd "$REPO"
rm -rf RecompiledFuncs
mkdir -p RecompiledFuncs

echo "=== N64Recomp: the game ==="
"$BIN/N64Recomp" recomp/body-harvest.us.toml > RecompiledFuncs/recompile.log 2>&1 || {
    grep -v '^\[WARN\]' RecompiledFuncs/recompile.log | tail -30 >&2
    exit 1
}
grep -v '^\[WARN\]' RecompiledFuncs/recompile.log | tail -5 || true
echo "warnings     : $(grep -c '^\[WARN\]' RecompiledFuncs/recompile.log || true)"

tail -1 RecompiledFuncs/funcs.h | grep -q '#endif' || {
    echo "funcs.h is truncated -- the recompiler died mid-write" >&2
    exit 1
}

echo "=== reference symbols for patches ==="
mkdir -p RecompiledFuncs/context
(cd RecompiledFuncs/context && "$BIN/N64Recomp" ../../recomp/body-harvest.us.toml --dump-context > /dev/null)

echo "=== declarations for runtime-provided libultra ==="
python3 tools/gen_reimplemented_decls.py

echo "=== runtime-provided libultra at cartridge addresses ==="
python3 tools/gen_runtime_func_table.py

echo "=== RSPRecomp: the audio microcode ==="
"$BIN/RSPRecomp" recomp/aspMain.us.toml

echo
python3 tools/count_recompiled.py
