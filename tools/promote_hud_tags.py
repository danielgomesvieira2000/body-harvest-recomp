"""Promote the tags you saved in the HUD inspector into the port's built-in table.

From Wave Race 64: Recompiled's tools/promote_hud_tags.py, pointed at this port's
table (load_defaults() in src/inspector.cpp) and settings folder.

The F1 panel's **Save to hud.json** writes to the per-user settings folder
(%LOCALAPPDATA%\\body-harvest-recomp on Windows), outside the repository and every
build directory. The tags follow you between builds while you work, but they exist
only on that machine, and a release carries nothing. This copies them into
load_defaults(), which is compiled into the executable. Run it, look at the diff,
commit.

    python tools/promote_hud_tags.py            # promote
    python tools/promote_hud_tags.py --dry-run  # show what it would do
    python tools/promote_hud_tags.py --file X   # read a hud.json from elsewhere
    python tools/promote_hud_tags.py --clear    # ... and empty the local file

Only the block between the two markers is rewritten; running it twice makes no
further change.

**Promotion is additive.** What is already in the block stays, and the local file
is merged into it; an identity in both takes the local file's class. That keeps
the loop working: tag, promote --clear, tag more, promote. --replace starts the
block from the local file instead.

**Identities are lower-cased**, as the panel writes them (tex:0x802866f8).

--clear empties the local hud.json after promoting. A local file that repeats the
promoted tags keeps overriding the built-in table even after it changes.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "src" / "inspector.cpp"
SETTINGS_NAME = "body-harvest-recomp"

BEGIN = "    // ---- promoted from hud.json by tools/promote_hud_tags.py ----"
END = "    // ---- end promoted ----"

CLASSES = {
    "center": "kAuto",
    "left": "kLeft",
    "right": "kRight",
    "stretch": "kStretch",
    "spill": "kSpill",
}
BY_EXPRESSION = {v: k for k, v in CLASSES.items()}

ENTRY = re.compile(r'^\s*by_identity\["([^"]+)"\]\s*=\s*(k\w+)\s*;')


def settings_directory():
    """The same directory bh::settings_directory() picks (src/main.cpp)."""
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if base:
            return Path(base) / SETTINGS_NAME
    else:
        base = os.environ.get("XDG_DATA_HOME")
        if base:
            return Path(base) / SETTINGS_NAME
        home = os.environ.get("HOME")
        if home:
            return Path(home) / ".local" / "share" / SETTINGS_NAME
    return Path.cwd()


def read_tags(path):
    if not path.exists():
        sys.exit(f"no hud.json at {path}\n"
                 f"Press F1 in the game, tag something, and press Save to hud.json.")
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as e:
        sys.exit(f"{path} is not valid JSON: {e}")
    if not isinstance(doc, dict):
        sys.exit(f"{path} does not hold an object")
    tags = []
    for name in CLASSES:
        for identity in doc.get(name, []) or []:
            if isinstance(identity, str) and identity.strip():
                tags.append((identity.strip().lower(), name))
    tags.sort()
    return doc, tags


def build_block(tags):
    lines = [BEGIN]
    if tags:
        width = max(len(i) for i, _ in tags)
        for identity, name in tags:
            key = '"%s"]' % identity
            lines.append("    by_identity[%-*s = %s;" % (width + 3, key, CLASSES[name]))
    else:
        lines.append("    // (nothing promoted yet)")
    lines.append(END)
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--file", type=Path, help="a hud.json to read instead of the local one")
    ap.add_argument("--dry-run", action="store_true", help="print the block, change nothing")
    ap.add_argument("--clear", action="store_true", help="empty the local hud.json afterwards")
    ap.add_argument("--replace", action="store_true",
                    help="start the block from the local file instead of merging into it")
    args = ap.parse_args()

    source = args.file if args.file is not None else settings_directory() / "hud.json"
    doc, tags = read_tags(source)
    print(f"  {source}: {len(tags)} tag(s)")

    text = TARGET.read_text(encoding="utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)
    if BEGIN not in lines or END not in lines:
        sys.exit(f"the promoted-block markers are missing from {TARGET}; restore them in load_defaults()")

    merged = {}
    start, stop = lines.index(BEGIN), lines.index(END)
    if not args.replace:
        for line in lines[start:stop]:
            m = ENTRY.match(line)
            if m and m.group(2) in BY_EXPRESSION:
                merged[m.group(1)] = BY_EXPRESSION[m.group(2)]
        if merged:
            print(f"  {len(merged)} tag(s) already promoted; merging")
    for identity, name in tags:
        if identity in merged and merged[identity] != name:
            print(f"  note: {identity} was {merged[identity]}, now {name}")
        merged[identity] = name

    block = build_block(sorted(merged.items()))
    lines[start:stop + 1] = block
    updated = newline.join(lines)

    if args.dry_run:
        print()
        print(newline.join(block))
        return
    if updated == text:
        print(f"  {TARGET.name} already up to date")
    else:
        TARGET.write_bytes(updated.encode("utf-8"))
        print(f"  {TARGET.name} updated -- review the diff and commit")

    if args.clear:
        for name in CLASSES:
            doc[name] = []
        source.write_bytes((json.dumps(doc, indent=4) + "\n").encode("utf-8"))
        print(f"  {source} emptied")

    print("\nRebuild to pick it up.")


if __name__ == "__main__":
    main()
