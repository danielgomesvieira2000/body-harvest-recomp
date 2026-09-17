# Changelog

Every release, newest first. Each entry links to its notes under
[`docs/releases/`](docs/releases).

Versions follow [semantic versioning](https://semver.org) loosely: while the
project is below 1.0, the minor number moves when something a player would
notice changes.

## 0.1.0 — First release (2026-09-17)

[Notes](docs/releases/0.1.0.md). The first public build; only the start of Greece has been played.

- **Boots and plays**: intro, title, three EEPROM save slots, name entry, difficulty, Greece
  gameplay on foot, entering and leaving houses.
- **Widescreen**: gameplay drawn at the full 320x240 (the game asked the video interface to stretch a
  304x230 region, which the runtime cannot); a genuinely wider view with the game's cull angles widened
  to match, outdoors and indoors; full-screen menu backdrops and the sky stretched. `BH_FULL_FRAME=0`,
  `BH_NO_WIDE_CULL=1`, `BH_NO_WIDE_INSIDE_CULL=1`, `BH_NO_BG_STRETCH=1`, `BH_NO_SKY_STRETCH=1` revert each.
- **HUD anchors**: radar right; health/alien bars and weapon panel left (`BH_NO_HUD_ANCHORS=1`).
- **High frame rate** through RT64's interpolation (gameplay runs at 20 frames per second), with the
  player model, shadow and aiming reticle steady.
- **Rumble** modelled as the game's pulsed motor, with a strength setting.
- **Frontend** from Wave Race 64: Recompiled: launcher, settings, remapping, mods, F1 debug menu with a
  live HUD editor.
- Fixed before release: pause map crash, door crash entering buildings.
