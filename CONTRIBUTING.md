# Contributing

Patches, bug reports and findings are welcome. One rule here is absolute, and
mistakes under it are expensive to undo.

## No copyrighted game material, ever

The project ships tooling and original code. The user supplies their own dump;
the game's assets are read from it at run time.

| Material | Where it lives |
|---|---|
| A dump (`.z64`, `.n64`, `.v64`), whole or in part, compressed or renamed | your machine only |
| Anything derived from a dump: `RecompiledFuncs/`, `RecompiledPatches/`, `elf/`, the decomp's build output (in its WSL mirror), `.sym`, `.map`, `dump.toml` | generated locally, already `.gitignore`d |
| Assets extracted from a dump: textures, models, audio, text, level data | nowhere in this repo |
| Assets **derived** from those: upscaled or AI-enhanced textures, retouched sprites, remastered audio, re-modelled geometry | nowhere in this repo, a release, or an issue |
| Screenshots and video of the game | attached to the issue or PR, not committed |
| Code copied from a decompilation that publishes no license | nowhere in this repo |

Upscaled texture packs are the common accident: a texture upscaled from a rip is
a derivative of the game's artwork. A tool that builds a pack **on the user's
machine from the user's own dump** is fine; the pack itself is not.

Original artwork made for the port (launcher background, icons) is welcome under
`assets/`, with its origin stated in the pull request.

**Facts about the ROM are not the ROM.** Addresses, segment layouts, symbol
names, struct fields, formats and the measurements behind them belong in
`docs/GAME-INTERNALS.md`. Bulk data -- byte dumps, captured display lists,
transcribed tables -- does not.

## Check before you push

`.gitignore` is the first line of defence, not a guarantee.

```bash
git diff --cached --name-status
git rev-list --objects main..HEAD |
    git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' |
    sort -k3 -n -r | head -20
```

Nothing this project tracks is large; a megabyte in a diff is worth a second look.
**If game data lands in a commit, say so at once** -- a later delete does not
remove it from history. The branch is rebuilt clean instead.

## Rules for changes

- **Generated code is never hand-edited.** Fix the config, a `patches/` function,
  or a script in `tools/`.
- **The decompilation (`lib/bh-decomp`) is never changed**; it publishes no license, so nothing
  from it is copied into this repository either.
- **Submodules are never hand-edited.** Every change to one is an idempotent
  script in `tools/` (`patch_*.py`); a submodule update reverts a hand edit silently.
- **The technical reference stays current.** A change that discovers a game fact
  updates `docs/GAME-INTERNALS.md`; one that fixes or explains something in the
  toolchain, runtime or renderer updates `docs/PORTING.md` -- in the same commit.
- A change that could make things worse on someone else's machine gets an
  environment-variable switch (`BH_NO_...`), so a report can bisect it.
- Match the file you are editing.

## Third-party code

Add libraries as submodules under `lib/`, with a row in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and a line in
`tools/third_party_licenses.txt`. A library that publishes no license cannot be used.

## Commits and pull requests

Subject: what changed. Body: why, and what was measured. One change per pull
request, and say what you tested. AI-written code is fine -- this project was
built with Claude Code -- and the code is still yours to have read.

## Reporting a bug

Use [docs/issues/TEMPLATE.md](docs/issues/TEMPLATE.md) as the shape. Include the
settings in use and the log from the settings folder. **Never attach a dump.**

## Licensing

Contributions to this project's own files are under the MIT License in `LICENSE`.
A built executable is a GPL-3.0 combined work; see THIRD_PARTY_NOTICES.md.
