# Body Harvest: Recompiled

> **Status: early release (0.1.0), very untested.** It boots and plays the start of Greece; most of the
> game has not been played in the port yet.

A native PC port of Body Harvest for Windows and Linux, made by statically recompiling the game with
[N64Recomp](https://github.com/N64Recomp/N64Recomp). Unofficial, and not affiliated with the game's
rights holders (DMA Design / Gremlin Interactive).

**No game data is included.** You need your own dump of **Body Harvest (USA)**
(SHA-1 `bbb6666f5014a473747ee4145f036d9fb25d7348`), as `.z64`, `.n64`, `.v64` or a ZIP. No other version
works.

## Features

- Widescreen at your window's aspect ratio: a genuinely wider view of the world, with the game's own
  culling widened to match, and the full-screen menu backdrops filling the screen
- High frame rate: smooth motion up to your display's refresh rate, with the game's own timing
- Launcher with graphics, sound and controls settings, remapping and mod support
- Keyboard and controller support; the keyboard always plays, pads join as they are plugged in
- EEPROM saves and the Rumble Pak, with a rumble strength setting
- F1 debug menu with a live HUD editor

- HUD anchored to the screen edges in widescreen (radar, bars, weapon panel)

## Getting started

Download the Windows or Linux package from
[Releases](https://github.com/danielgomesvieira2000/body-harvest-recomp/releases/latest). Windows: unzip
and run `body-harvest-recomp.exe`. Linux: extract and run `./body-harvest-recomp.sh`. Pick your dump in the
launcher.

To build it yourself:

Linux (Debian/Ubuntu, or WSL):

```sh
bash tools/setup_linux.sh --install
bash tools/build_linux.sh "/path/to/Body Harvest (USA).z64"
./build-linux/body-harvest-recomp
```

Windows needs Visual Studio Build Tools with clang-cl, CMake, Ninja, Python and WSL (the decompilation's
build and the recompiler run there); the steps are in [docs/BUILDING.md](docs/BUILDING.md), which also
lists the default controls.

## Documentation

- [Building](docs/BUILDING.md)
- [How the port works](docs/PORTING.md)
- [Game internals](docs/GAME-INTERNALS.md)
- [Plan](docs/PLAN.md) and the working record in [docs/findings/](docs/findings)
- [Changelog](CHANGELOG.md)

## Credits

- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy and contributors
- [RT64](https://github.com/rt64/rt64) by Dario and contributors
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp contributors
- [PromptFont](https://github.com/Shinmera/promptfont) by Yukari "Shinmera" Hafner, for the controller glyphs
- [body-harvest-decompilation](https://github.com/jaytheham/body-harvest-decompilation) by jaytheham and
  contributors: its matching build is what the recompiler reads, and its names are used throughout
- [BodyHarvest-PC-Port](https://github.com/12feihu/BodyHarvest-PC-Port) by 12feihu, whose notes on RT64's
  L3DEX handling, the audio microcode and the game's culling pointed this port in the right direction
- The launcher, harness and tooling come from [Wave Race 64: Recompiled](https://github.com/danielgomesvieira2000/wave-race-64-recomp)

Every third-party component and its license is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## AI use

This project was built with Claude Code. Its code, patches, tools, documentation and artwork were written
by Claude (Anthropic), directed and tested by Daniel Gomes Vieira. The libraries and research it builds
on are the work of the people credited above.

## License

The project's own code is [MIT](LICENSE). A built executable links N64ModernRuntime (GPL-3.0), so a
distributed binary is a GPL-3.0 combined work whose source is this repository at the release's tag. The
executable contains the game's code, recompiled from a dump; Body Harvest is the property of its rights
holders.
