#!/usr/bin/env python3
"""Run a port's executable in a throwaway copy, so a test leaves nothing behind.

    python tools/test_sandbox.py --exe build/body-harvest-recomp.exe --title "Body Harvest: Recompiled" \
        --rom rom.z64 --capture 20 30 --out shots/run1 [--env BH_INPUT_SCRIPT=tools/scripts/x.txt] \
        [--seed path/to/hud.json] [--grab 10,20,30] [--keep]

    python tools/test_sandbox.py --exe build/... --rom rom.z64 --seconds 45 --out shots/run2

Why: a test that drops `portable.txt` beside a shared build redirects the settings of whoever launches
that build next. On Hybrid Heaven a leftover one captured Daniel's own session, and the cleanup that
followed deleted his Controller Pak (playbook 10, working-style.md "Test runs leave nothing behind").

What it does:
1. Copies the executable's directory (exe, DLLs, `assets/`, nothing else of the build tree) into a new
   temporary folder, and writes `portable.txt` there. Every port that reads `portable.txt` from the
   exe directory or from a working directory set to it (Wave Race, Pilotwings, Beetle, Body Harvest)
   then keeps settings, the dump copy and saves inside the sandbox. Rayman 2 has no portable mode:
   this tool does not isolate it.
2. Copies each `--seed` file into the sandbox (a pak, a hud.json, a graphics.json).
3. Runs it: with `--capture FROM TO` through `capture_frames.py` (frames + `game.log` in `--out`),
   otherwise for `--seconds` with output to `--out/game.log`; `--grab 10,20` also photographs the
   window at those seconds with `print_window.py` (PrintWindow: the window's own contents, covered or not).
   Body Harvest added --grab: `capture_frames.py` saved 0 frames of this port's window on this machine.
4. Copies the sandbox's settings files that changed into `--out/settings/` (so a test's saves can
   be inspected), then deletes the sandbox, unless `--keep`.

The real build directory and the player's settings folder are never written.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD_ONLY = {".obj", ".o", ".lib", ".a", ".ilk", ".exp", ".ninja", ".cmake", ".log"}
SKIP_DIRS = {"CMakeFiles", "lib", "src", "shaders", ".cmake"}


def copy_runtime(exe: Path, sandbox: Path):
    """The executable, the files beside it that a player's package would hold, and assets/."""
    src_dir = exe.parent
    for entry in src_dir.iterdir():
        if entry.is_dir():
            if entry.name == "assets":
                shutil.copytree(entry, sandbox / "assets")
            continue
        if entry.suffix.lower() in BUILD_ONLY or entry.name in {"build.ninja", "CMakeCache.txt",
                                                                "compile_commands.json", "portable.txt"}:
            continue
        # Executables, shared libraries and symbol files: a crash in the sandbox symbolises too.
        if entry == exe or entry.suffix.lower() in {".dll", ".so", ".pdb", ".debug", ".dylib"}:
            shutil.copy2(entry, sandbox / entry.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--rom", type=Path, help="dump passed on the command line (skips the launcher)")
    ap.add_argument("--title", help="window title; required with --capture")
    ap.add_argument("--capture", nargs=2, type=float, metavar=("FROM", "TO"))
    ap.add_argument("--seconds", type=float, default=30.0, help="run time without --capture")
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    ap.add_argument("--seed", action="append", default=[], type=Path, help="file copied into the sandbox")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--keep", action="store_true", help="leave the sandbox for inspection")
    ap.add_argument("--grab", default="", help="comma-separated seconds at which to grab the window")
    args = ap.parse_args()

    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f"no executable at {exe}")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    sandbox = Path(tempfile.mkdtemp(prefix="port-sandbox-"))
    try:
        copy_runtime(exe, sandbox)
        (sandbox / "portable.txt").write_text("", encoding="utf-8")
        for seed in args.seed:
            shutil.copy2(seed, sandbox / seed.name)
        started = time.time()
        sandbox_exe = sandbox / exe.name
        rom = str(args.rom.resolve()) if args.rom else None
        print(f"sandbox: {sandbox}")

        if args.capture:
            if not args.title:
                sys.exit("--capture needs --title")
            cmd = [sys.executable, str(HERE / "capture_frames.py"), str(out),
                   str(args.capture[0]), str(args.capture[1]),
                   "--exe", str(sandbox_exe), "--title", args.title, "--scale", str(args.scale)]
            if rom:
                cmd += ["--rom", rom]
            for item in args.env:
                cmd += ["--env", item]
            subprocess.run(cmd, check=False)
        else:
            env = dict(os.environ)
            for item in args.env:
                name, _, value = item.partition("=")
                if value:
                    env[name] = value
                else:
                    env.pop(name, None)
            with open(out / "game.log", "w") as log:
                proc = subprocess.Popen([str(sandbox_exe)] + ([rom] if rom else []), cwd=sandbox, env=env,
                                        stdout=log, stderr=subprocess.STDOUT)
                grabs = sorted(float(s) for s in args.grab.split(",") if s.strip())
                launched = time.time()
                for at in grabs:
                    delay = at - (time.time() - launched)
                    if delay > 0:
                        time.sleep(delay)
                    if proc.poll() is not None:
                        break
                    png = out / f"grab_t{at:05.1f}s.png"
                    # The window's own contents (PrintWindow), so a covering window
                    # does not end up in the picture (tools/print_window.py).
                    subprocess.run([sys.executable, str(HERE / "print_window.py"), str(png), str(proc.pid)],
                                   check=False, stdout=subprocess.DEVNULL)
                try:
                    proc.wait(timeout=max(0.0, args.seconds - (time.time() - launched)))
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

        # What the run wrote, kept for inspection (never game dumps).
        kept = out / "settings"
        for path in sandbox.rglob("*"):
            if not path.is_file() or path.stat().st_mtime < started:
                continue
            if path.suffix.lower() in {".z64", ".n64", ".v64"}:
                continue
            target = kept / path.relative_to(sandbox)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        print(f"output: {out}")
    finally:
        if args.keep:
            print(f"kept sandbox: {sandbox}")
        else:
            shutil.rmtree(sandbox, ignore_errors=True)


if __name__ == "__main__":
    main()
