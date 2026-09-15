#!/usr/bin/env bash
# Build the Linux port. On the first build, pass your dump. From Pilotwings 64:
# Recompiled's script, with this port's pipeline (docs/BUILDING.md).
#
#   bash tools/setup_linux.sh
#   bash tools/build_linux.sh "/path/to/Body Harvest (USA).z64"
#   ./build-linux/body-harvest-recomp
#
# Afterwards, source-only rebuilds need no argument: the generated sources are
# already there and only change when the dump or recomp/*.toml does.
#
# Environment:
#   BH_BUILD_DIR   where to build (default build-linux)
#   BH_JOBS        parallelism (default: the number of processors)
#   BH_CC, BH_CXX  the compiler, instead of the newest Clang found
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

if [ "$(uname -s)" != Linux ]; then
    echo "This script is for Linux. On Windows, see docs/BUILDING.md." >&2
    exit 1
fi

BUILD_DIR="${BH_BUILD_DIR:-build-linux}"
JOBS="${BH_JOBS:-$(nproc)}"

for tool in cmake ninja python3; do
    command -v "$tool" > /dev/null || {
        echo "$tool is not on PATH. Run: bash tools/setup_linux.sh" >&2
        exit 1
    }
done

# Which Clang: an explicit choice, then the plain names, then the highest
# versioned clang-NN present (Debian and Ubuntu often lack the unversioned names).
if [ -n "${BH_CC:-}" ] && [ -n "${BH_CXX:-}" ]; then
    CC="$BH_CC"
    CXX="$BH_CXX"
elif command -v clang > /dev/null && command -v clang++ > /dev/null; then
    CC=clang
    CXX=clang++
else
    CC=""
    for candidate in $(ls /usr/bin/clang-[0-9]* 2> /dev/null | sort -V -r); do
        version="${candidate##*/clang-}"
        case "$version" in
            *[!0-9]*) continue ;;   # clang-format-21, clang-tidy-21, and friends
        esac
        if [ -x "/usr/bin/clang++-$version" ]; then
            CC="$candidate"
            CXX="/usr/bin/clang++-$version"
            break
        fi
    done
    if [ -z "$CC" ]; then
        echo "No Clang found. Install one -- apt install clang -- or set BH_CC" >&2
        echo "and BH_CXX to the compiler you want to use." >&2
        exit 1
    fi
fi
echo "compiler: $CC / $CXX"

# --------------------------------------------------------------- patches ----
# Idempotent, and re-applied on every build: they patch submodules, and a
# submodule update reverts them silently.
echo "=== submodule patches ==="
python3 tools/patch_all.py

# ------------------------------------------------------- generated sources ----
if [ $# -gt 0 ]; then
    dump="$(realpath "$1")"
    if [ "$dump" != "$REPO/rom.z64" ]; then
        cp "$dump" "$REPO/rom.z64"      # git-ignored; the RSP config reads it from here
    fi
    echo
    echo "=== recompilers ==="
    bash tools/wsl_build_recompiler.sh
    echo
    echo "=== unpack, split, ELF, recompile ==="
    python3 tools/unpack_rom.py rom.z64
    bash tools/regenerate.sh
elif [ ! -f RecompiledFuncs/funcs.h ]; then
    echo "First build: pass your dump." >&2
    echo "    bash tools/build_linux.sh \"/path/to/Body Harvest (USA).z64\"" >&2
    exit 1
fi

# ------------------------------------------------------------ configure ----
# CMAKE_POLICY_VERSION_MINIMUM is for CMake 4, which refuses the
# cmake_minimum_required(VERSION <3.5) still declared under lib/RT64/src/contrib.
echo
echo "=== configure ==="
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBH_WITH_RUNTIME=ON -DBH_WITH_RECOMPILED=ON -DBH_WITH_FRONTEND=ON

echo
echo "=== build ==="
cmake --build "$BUILD_DIR" --target body-harvest-recomp -j "$JOBS"

echo
echo "Built: $BUILD_DIR/body-harvest-recomp"
echo "Settings and saves live in \${XDG_DATA_HOME:-~/.local/share}/body-harvest-recomp."
