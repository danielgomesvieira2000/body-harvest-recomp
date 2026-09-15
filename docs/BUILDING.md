# Building Body Harvest: Recompiled

Step by step. Every step that touches the dump runs on your machine; nothing it produces is committed.

You need your own dump of **Body Harvest (USA)**, SHA-1
`bbb6666f5014a473747ee4145f036d9fb25d7348`. Put it in the repository root as `rom.z64`
(git-ignored).

## Requirements

| Tool | Where | Notes |
|---|---|---|
| Git | Windows | submodules |
| CMake ≥ 3.20, Ninja | Windows | |
| LLVM (clang-cl) | Windows | **clang-cl, not clang++** (RT64/RecompFrontend add `/W4`); never GCC |
| Visual Studio 2022 Build Tools | Windows | Windows SDK + CRT for clang-cl |
| Python ≥ 3.10 | Windows | `tools/` |
| WSL2 Ubuntu with `cmake ninja-build clang binutils-mips-linux-gnu gcc python3 python3-venv rsync` | WSL | the decompilation's build (IDO recompiled, splat) and N64Recomp/RSPRecomp run under Linux |

`powershell -File tools/check_toolchain.ps1` reports what is present.

## 1. Clone and patch the submodules

```powershell
git clone <repo> body-harvest-recomp
cd body-harvest-recomp
git submodule update --init lib/bh-decomp
git submodule update --init --recursive lib/N64ModernRuntime lib/RT64 lib/RecompFrontend
python tools/patch_all.py      # idempotent; rerun after any submodule update
```

Not `--recurse-submodules` on the clone: the decompilation records a gitlink (`tools/n64`) with no
`.gitmodules` entry, and git refuses to recurse into it. It needs none of its own submodules to build.

`git submodule status` must show:

| Submodule | Commit |
|---|---|
| `lib/N64ModernRuntime` (upstream) | `cdf5abb` |
| `lib/RT64` | `5473732` |
| `lib/RecompFrontend` | `b1a1477` |
| `lib/bh-decomp` (jaytheham/body-harvest-decompilation) | `4600677` |

If a submodule shows anything else (a fresh `git submodule add` checks out upstream HEAD), check out the
commit above in it and update its own submodules.

## 2. Check the dump

```powershell
python tools/identify_rom.py rom.z64      # exits 0 only for the supported dump
```

## 3. Dump to generated sources (WSL)

```powershell
wsl -d Ubuntu -e bash tools/regenerate.sh
```

In order, each stage checking its own result:

| Stage | Script | Ends with |
|---|---|---|
| N64Recomp + RSPRecomp, if not built | `tools/wsl_build_recompiler.sh` | two executables in `lib/N64ModernRuntime/N64Recomp/build-linux/` |
| the decompilation's matching build | `tools/wsl_build_elf.sh` (mirrors `lib/bh-decomp` to `~/.cache/body-harvest-recomp/decomp`, creates its Python venv the first time) | `build/bh.us.z64: OK` |
| ELF repair | `tools/fix_elf.py` | `end state: 3045 FUNC, 0 ABS, 0 size 0, 0 duplicate addresses` |
| ELF verification | `tools/verify_elf.py` | `ELF verified` |
| recompile | `tools/recompile.sh` | `functions emitted : 2920 (expected 2920)` |

After a change to `recomp/*.toml` only the last stage is needed: `wsl -d Ubuntu -e bash tools/recompile.sh`.

## 4. Configure and build (Windows)

```powershell
cmake -B build -G Ninja "-DCMAKE_C_COMPILER=clang-cl" "-DCMAKE_CXX_COMPILER=clang-cl" "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DBH_WITH_RECOMPILED=ON" "-DBH_WITH_RUNTIME=ON" "-DBH_WITH_FRONTEND=ON"
cmake --build build --target body-harvest-recomp
build\body-harvest-recomp.exe            # the launcher: Load ROM, then Start Game
build\body-harvest-recomp.exe rom.z64    # straight into the game (scripted runs)
```

Quote every `-D` argument in PowerShell (it splits `3.5` at the dot). Never build Debug: it breaks audio
timing. Delete `build/` when switching compilers. Close a running copy before rebuilding. Started without
a terminal, the game writes its log to `%LOCALAPPDATA%\body-harvest-recomp\bh.log`.

With every option off the tree still builds an executable that only identifies dumps
(`build\body-harvest-recomp.exe --identify rom.z64`); without `-DBH_WITH_FRONTEND=ON` it has no launcher
and needs a dump on the command line.

## 5. Controls

| N64 | Keyboard | Pad |
|---|---|---|
| Stick | arrow keys | left stick |
| A / B | X / C | A (south) / X (west) |
| Z | Z | left trigger |
| Start | Enter | Start |
| L / R | A / S | left shoulder / right trigger |
| C buttons | I J K L | right stick |
| D-pad | T F G H | D-pad |
| Menu | Escape | Back / View |

F1 opens RT64's debug menu and the HUD inspector. Everything is remappable in Controls. The keyboard
always plays, with or without a pad; every connected pad controls the game too.

## 6. Linux

On Debian or Ubuntu (WSL works too):

```sh
bash tools/setup_linux.sh --install      # packages and submodules
bash tools/build_linux.sh "/path/to/Body Harvest (USA).z64"
./build-linux/body-harvest-recomp
```

The dump is needed on the first build (it builds the decompilation's ELF and recompiles) and again only
after a `recomp/*.toml` change; later rebuilds are `bash tools/build_linux.sh`. Clang only (the newest
`clang-NN` found, or `BH_CC`/`BH_CXX`); `BH_BUILD_DIR`, `BH_JOBS`. Settings and saves live in
`${XDG_DATA_HOME:-~/.local/share}/body-harvest-recomp`. The renderer needs a Vulkan driver.

## 7. Packaging a release

```powershell
powershell -ExecutionPolicy Bypass -File tools/package_release.ps1 -BuildDir build -Version X.Y.Z
wsl -d Ubuntu -- python3 tools/package_release.py --version X.Y.Z
```

Outputs in `dist/`: `body-harvest-recomp-X.Y.Z-windows-x64.zip` (+ `-debug-symbols.zip`) and
`body-harvest-recomp-X.Y.Z-linux-x86_64.tar.gz` (+ `-debug-symbols.tar.gz`). Both refuse to stage a dump
or a save and copy the license texts listed in `tools/third_party_licenses.txt`. The Linux script refuses
to overwrite an archive.
