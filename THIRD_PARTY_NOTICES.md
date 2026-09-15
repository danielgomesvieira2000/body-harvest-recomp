# Third-party notices

This project's own code (`src/`, `include/`, `patches/`, `tools/`, `recomp/`,
original artwork under `assets/`) is under the MIT License in `LICENSE`. A built
executable also contains, or ships beside, the components below. **Every release
carries their license texts in `licenses/`**, copied from the list in
`tools/third_party_licenses.txt`.

Portions of this software are copyright © The FreeType Project
(www.freetype.org). All rights reserved.

## Runtime, renderer and frontend

| Component | Role | License | Text |
|---|---|---|---|
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (librecomp, ultramodern; Daniel Gomes Vieira's fork, `controller-pak` branch) | the runtime the recompiled game runs on | **GPL-3.0** | `lib/N64ModernRuntime/COPYING` |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp), RSPRecomp | the recompiler; its runtime headers are linked in | MIT | `lib/N64ModernRuntime/N64Recomp/LICENSE` |
| [RT64](https://github.com/rt64/rt64) and [plume](https://github.com/renderbag/plume) | the renderer | MIT | `lib/RT64/LICENSE`, `lib/RT64/src/contrib/plume/LICENSE` |
| [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) (recompui, recompinput) | launcher, menus, input | **no license published** | -- (see *Open points*) |
| [RmlUi](https://github.com/mikke89/RmlUi) | UI toolkit under recompui | MIT | `lib/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt` |
| [FreeType](https://freetype.org) | font rasteriser | FTL | `lib/RecompFrontend/recompui/lib/freetype-windows-binaries/LICENSE.TXT`, `FTL.TXT` |
| [lunasvg](https://github.com/sammycage/lunasvg), plutovg | SVG icons | MIT (plutovg rasteriser: FTL) | `lib/RecompFrontend/recompui/lib/lunasvg/LICENSE` |
| [SDL2](https://libsdl.org) | window, input, audio | Zlib | `lib/RT64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt` |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) (dxcompiler.dll, dxil.dll) | shader compilation, Windows | Microsoft terms, with MIT and LLVM parts | `licenses/dxc-*.txt` |

## Libraries compiled in

| Component | Role | License | Text |
|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui), [ImPlot](https://github.com/epezent/implot), [im3d](https://github.com/john-chapman/im3d) | RT64's debug menu (F1) | MIT | `lib/RT64/src/contrib/imgui/LICENSE.txt`, `.../implot/LICENSE`, `.../im3d/LICENSE` |
| [hlsl++](https://github.com/redorav/hlslpp) | RT64's shader-style math | MIT | `lib/RT64/src/contrib/hlslpp/LICENSE` |
| [nlohmann/json](https://github.com/nlohmann/json) | settings, texture packs, profiles | MIT | `licenses/nlohmann-json.txt` |
| [stb_image](https://github.com/nothings/stb) (and stb_truetype in plutovg) | texture pack images, SVG fonts | MIT or Unlicense | `lib/RT64/src/contrib/stb/LICENSE` |
| [ddspp](https://github.com/redorav/ddspp) | DDS textures in packs | MIT | `lib/RT64/src/contrib/ddspp/LICENSE` |
| [xxHash](https://github.com/Cyan4973/xxHash) | texture hashing | BSD-2-Clause | `lib/RT64/src/contrib/xxHash/LICENSE` |
| [zstd](https://github.com/facebook/zstd) | compression | BSD-3-Clause (dual GPL-2.0; used under BSD) | `lib/RT64/src/contrib/zstd/LICENSE` |
| [miniz](https://github.com/richgel999/miniz) | zip reading for mods (librecomp's copy, MIT) and RT64 (its copy, Unlicense) | MIT / Unlicense | `lib/N64ModernRuntime/thirdparty/miniz/LICENSE`, `licenses/miniz-rt64.txt` |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | runtime heap | MIT | `lib/N64ModernRuntime/thirdparty/o1heap/LICENSE` |
| [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | runtime and UI queues | BSD-2-Clause (or Boost 1.0) | `licenses/concurrentqueue.txt` |
| [{fmt}](https://github.com/fmtlib/fmt), [rabbitizer](https://github.com/Decompollaborate/rabbitizer), [sljit](https://github.com/zherczeg/sljit) | LiveRecomp, for code mods | MIT, MIT, BSD-2-Clause | `lib/N64ModernRuntime/N64Recomp/lib/{fmt,rabbitizer,sljit}/LICENSE` |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | the file picker for the dump | Zlib | `lib/RT64/src/contrib/nativefiledialog-extended/LICENSE` |
| [re-spirv](https://github.com/renderbag/re-spirv), with Khronos SPIRV-Headers | SPIR-V shader specialisation | MIT; SPIRV-Headers under Khronos's MIT-style license | `lib/RT64/src/contrib/re-spirv/LICENSE`, `.../external/SPIRV-Headers/LICENSE` |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers), [volk](https://github.com/zeux/volk), [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | the Vulkan backend | Apache-2.0, MIT, MIT | `lib/RT64/src/contrib/plume/contrib/{Vulkan-Headers/LICENSE.md, volk/LICENSE.md, VulkanMemoryAllocator/LICENSE.txt}` |
| [D3D12MemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | the D3D12 backend, Windows | MIT | `lib/RT64/src/contrib/plume/contrib/D3D12MemoryAllocator/LICENSE.txt` |
| [utf8conv](https://github.com/GiovanniDicanio/UTF8Conv) (Giovanni Dicanio) | UTF-8/UTF-16 conversion in RT64, Windows | MIT | `licenses/utf8conv.txt` (from upstream; the copy in RT64 carries only a copyright line) |
| [ares](https://github.com/ares-emulator/ares) RSP vector unit | librecomp's RSP instruction emulation, compiled with the audio microcode | ISC | `licenses/ares-rsp.txt` |
| [odashi/encoding](https://github.com/odashi/encoding) | EUC-JP conversion in librecomp | MIT | `licenses/odashi-encoding.txt` |
| metal-cpp | the Metal backend, macOS only | Apache-2.0 | `lib/RT64/src/contrib/plume/contrib/metal-cpp/LICENSE.txt` |

## Fonts

| Component | Role | License | Text |
|---|---|---|---|
| Lato, Noto Emoji | menu fonts, from RmlUi's samples | OFL 1.1 | `lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LICENSE.txt` |
| [PromptFont](https://github.com/Shinmera/promptfont) | controller glyphs | OFL 1.1 | `assets/promptfont/LICENSE.txt` |

## Symbol and decompilation sources

| Source | License | Use |
|---|---|---|
| [jaytheham/body-harvest-decompilation](https://github.com/jaytheham/body-harvest-decompilation) | **none published** | a git submodule (`lib/bh-decomp`, a pointer, not a copy); built on the user's machine from the user's dump to produce the ELF the recompiler reads; function names used in this port's code and docs. No file from it is committed here |
| [12feihu/BodyHarvest-PC-Port](https://github.com/12feihu/BodyHarvest-PC-Port) | MIT | findings only (RT64's L3DEX 1.x mapping, `aspMain` load address, hand-coded libultra leaves, the cull addresses), each re-derived in this port; no code copied |

## Open points

- **RecompFrontend publishes no license.** Without one, no permission to copy or
  redistribute it exists beyond what its authors grant. This project uses it as a
  submodule and does not vendor a copy; anyone distributing a built executable
  should resolve this with its authors first.
- **The DirectX Shader Compiler's Microsoft terms** attach conditions to
  redistributing its DLLs beside a GPL-3.0 executable; not established here.

## What this means for a built executable

The runtime is GPL-3.0 and statically linked, so **any executable built from this
project is a combined work under the GNU GPL, version 3**. Whoever distributes one
must make its corresponding source available: this repository at the commit the
binary was built from, with the submodules it pins.

## Artwork

`assets/icons/Logo.svg` and `assets/AppIcon.ico` are original artwork for this project, drawn
from polygons by `tools/make_logo.py` (written with Claude at the author's direction), under the
MIT License. Nothing in them is derived from the game.
