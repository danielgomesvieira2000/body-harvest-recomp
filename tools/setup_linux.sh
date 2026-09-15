#!/usr/bin/env bash
# One-time preparation for a Linux build: distribution packages and the
# submodules. From Pilotwings 64: Recompiled's script. The decomp's own Python
# environment is created by tools/wsl_build_elf.sh on its first run.
#
# What each package is for:
#
#   clang, lld            the port (N64Recomp's output is validated against Clang;
#                         GCC is never used, docs/PLAN.md).
#   cmake, ninja-build, pkg-config   the build.
#   python3, python3-venv the pipeline's tools and the decomp's build (splat).
#   binutils-mips-linux-gnu  the decomp's assembler and linker.
#   rsync                 mirroring lib/bh-decomp to where it is built.
#   libsdl2-dev           the window, the pad and the audio device.
#   libfreetype-dev       RmlUi's font engine, under recompui.
#   libgtk-3-dev          nativefiledialog-extended, the dump picker.
#   libvulkan-dev         RT64's renderer on Linux.
#   mesa-vulkan-drivers   a driver to run it (skip with a proprietary stack).
#   vulkan-tools          vulkaninfo, for when the renderer will not start.
#
# Nothing is installed without asking: with no arguments the script prints what
# is missing and the command that installs it. Pass --install to run it.
#
#   bash tools/setup_linux.sh [--install]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PACKAGES=(
    clang lld cmake ninja-build pkg-config git
    python3 python3-venv binutils-mips-linux-gnu rsync
    libsdl2-dev libfreetype-dev libgtk-3-dev
    libvulkan-dev vulkan-tools mesa-vulkan-drivers
)

install=0
[ "${1:-}" = "--install" ] && install=1

# ------------------------------------------------------------- packages ----
if command -v dpkg-query > /dev/null; then
    missing=()
    for p in "${PACKAGES[@]}"; do
        if dpkg-query -W -f='${Status}' "$p" 2> /dev/null | grep -q "install ok installed"; then
            continue
        fi
        # A versioned toolchain counts: Debian and Ubuntu install clang-21 and
        # friends without the unversioned metapackage.
        if [ "$p" = clang ] && ls /usr/bin/clang++-[0-9]* > /dev/null 2>&1; then
            continue
        fi
        if [ "$p" = lld ] && ls /usr/bin/ld.lld-[0-9]* /usr/lib/llvm-*/bin/ld.lld > /dev/null 2>&1; then
            continue
        fi
        missing+=("$p")
    done

    if [ ${#missing[@]} -eq 0 ]; then
        echo "packages: all present"
    elif [ "$install" -eq 1 ]; then
        echo "installing: ${missing[*]}"
        sudo apt-get update
        sudo apt-get install -y "${missing[@]}"
    else
        echo "missing packages: ${missing[*]}"
        echo
        echo "Install them with:"
        echo "    sudo apt-get install ${missing[*]}"
        echo "or re-run this script as: bash tools/setup_linux.sh --install"
        echo
    fi
else
    echo "Not a dpkg-based distribution -- install the equivalents of these yourself:"
    printf '    %s\n' "${PACKAGES[@]}"
    echo
fi

# ------------------------------------------------------------ submodules ----
echo "=== submodules ==="
# Not --recursive across everything: lib/bh-decomp records a gitlink (tools/n64)
# with no .gitmodules entry, and git refuses to recurse into it. The decomp needs
# none of its own submodules to build (its tools are committed).
git -C "$REPO" submodule update --init lib/bh-decomp
git -C "$REPO" submodule update --init --recursive lib/N64ModernRuntime lib/RT64 lib/RecompFrontend

echo
echo "Ready. Next:"
echo "    bash tools/build_linux.sh '/path/to/Body Harvest (USA).z64'"
