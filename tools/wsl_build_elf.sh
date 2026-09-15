#!/usr/bin/env bash
# Build the decompilation's matching ROM and ELF from the pinned submodule, then
# post-process and verify the ELF N64Recomp reads.
#
#   wsl -d Ubuntu -e bash tools/wsl_build_elf.sh [rom.z64]
#
# Route A (docs/PLAN.md D3): lib/bh-decomp is mirrored to the Linux filesystem
# (a build on /mnt/c crosses the 9P boundary on every file operation), the
# user's dump is placed as baserom.us.z64, and `make extract && make` must end
# with "build/bh.us.z64: OK" -- a ROM that matches byte for byte -- before its
# ELF is taken. The decomp is never modified; everything this port needs from
# the ELF is done afterwards by tools/fix_elf.py, and tools/verify_elf.py checks
# the result against the dump.
#
# Output (git-ignored): elf/bh.us.elf (as built), elf/bh.us.fixed.elf (what
# N64Recomp reads), elf/bh.us.yaml (the splat config the verifier reads).
#
# Env: BH_DECOMP_WORKDIR (default ~/.cache/body-harvest-recomp/decomp), BH_JOBS.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ROM="${1:-$REPO/rom.z64}"
WORK="${BH_DECOMP_WORKDIR:-$HOME/.cache/body-harvest-recomp/decomp}"
JOBS="${BH_JOBS:-$(nproc)}"
SRC="$REPO/lib/bh-decomp"

if [ ! -f "$SRC/Makefile" ]; then
    echo "lib/bh-decomp is missing: git submodule update --init lib/bh-decomp" >&2
    exit 1
fi
if [ ! -f "$ROM" ]; then
    echo "no dump at $ROM" >&2
    exit 1
fi
SHA=$(sha1sum "$ROM" | cut -d' ' -f1)
if [ "$SHA" != "bbb6666f5014a473747ee4145f036d9fb25d7348" ]; then
    echo "this is not Body Harvest (USA): sha1 $SHA" >&2
    exit 2
fi

echo "== mirror lib/bh-decomp -> $WORK"
mkdir -p "$WORK"
# The submodule's .git is a file pointing into this repository; the mirror does
# not need it. Build products and the venv in the mirror are kept.
rsync -a --delete \
    --exclude=.git --exclude=build/ --exclude=asm/ --exclude=assets/ --exclude=.venv/ \
    --exclude=baserom.us.z64 --exclude='*.o' --exclude='*.so' \
    --exclude=tools/rnc_propack_source/rnc64 \
    "$SRC/" "$WORK/"
cp "$ROM" "$WORK/baserom.us.z64"

cd "$WORK"
if [ ! -x .venv/bin/python3 ]; then
    echo "== python venv"
    python3 -m venv .venv
    # tree-sitter(-languages) serve m2c and the permuter only and have no wheel for
    # current Pythons; the build does not need them (findings/phase-00.md).
    grep -v tree-sitter requirements.txt > .venv/requirements.build.txt
    .venv/bin/pip install -q -r .venv/requirements.build.txt
fi
# shellcheck disable=SC1091
. .venv/bin/activate

echo "== make extract"
make extract > "$WORK/extract.log" 2>&1 || { tail -30 "$WORK/extract.log"; exit 1; }
echo "== make -j$JOBS"
make -j"$JOBS" > "$WORK/make.log" 2>&1 || { tail -30 "$WORK/make.log"; exit 1; }
if ! grep -q "build/bh.us.z64: OK" "$WORK/make.log"; then
    tail -20 "$WORK/make.log"
    echo "the decomp build does not match the dump; refusing its ELF" >&2
    exit 1
fi
echo "   build/bh.us.z64: OK"

mkdir -p "$REPO/elf"
cp build/bh.us.elf "$REPO/elf/bh.us.elf"
cp bh.us.yaml "$REPO/elf/bh.us.yaml"

echo "== fix_elf.py"
python3 "$REPO/tools/fix_elf.py" "$REPO/elf/bh.us.elf" "$REPO/elf/bh.us.fixed.elf"
echo "== verify_elf.py"
python3 "$REPO/tools/verify_elf.py" "$REPO/elf/bh.us.fixed.elf" "$ROM" "$REPO/elf/bh.us.yaml"
