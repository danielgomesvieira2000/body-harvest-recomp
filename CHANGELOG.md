# Changelog

Every release, newest first. Each entry links to its notes under
[`docs/releases/`](docs/releases).

Versions follow [semantic versioning](https://semver.org) loosely: while the
project is below 1.0, the minor number moves when something a player would
notice changes.

## Unreleased

Nothing has been released yet. What the development builds do so far:

- **Boots and plays**: intro, title, three EEPROM save slots, name entry, difficulty, Greece
  gameplay on foot and in a vehicle.
- **Widescreen**: gameplay drawn at the full 320x240 (the game asked the video interface to stretch a
  304x230 region, which the runtime cannot); a genuinely wider view with the game's cull angle widened
  to match; full-screen menu backdrops stretched. `BH_FULL_FRAME=0`, `BH_NO_WIDE_CULL=1`,
  `BH_NO_BG_STRETCH=1` revert each.
- **High frame rate** through RT64's interpolation (gameplay runs at 20 frames per second).
- **Frontend** from Wave Race 64: Recompiled: launcher, settings, remapping, mods, F1 debug menu with a
  live HUD editor.
- Known: HUD elements are not anchored to the screen edges yet; buildings, other levels, rumble and
  audio pitch are unchecked by a player.
