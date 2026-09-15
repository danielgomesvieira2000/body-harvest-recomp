#!/usr/bin/env python3
"""Resolve every `exe+0xOFFSET` in a port log to function and source line.

    python tools/symbolize_log.py <log> [--exe build/body-harvest-recomp.exe]

The thread sampler (BH_SAMPLE=1) and the crash handler print module offsets;
this runs llvm-symbolizer once over all of them against the RelWithDebInfo PDB
of the same build and prints `offset  count-in-log  function  file:line`.
"""
import argparse
import collections
import re
import subprocess
import sys

ap = argparse.ArgumentParser()
ap.add_argument("log")
ap.add_argument("--exe", default="build/body-harvest-recomp.exe")
args = ap.parse_args()
text = open(args.log, encoding="utf-8", errors="replace").read()
counts = collections.Counter()
for m in re.finditer(r"exe\+(0x[0-9a-fA-F]+)", text):
    counts[m.group(1).lower()] += 1
addrs = sorted(counts, key=lambda a: int(a, 16))
out = subprocess.run(["llvm-symbolizer", f"--obj={args.exe}", "--relative-address", "--demangle"],
                     input="\n".join(addrs) + "\n", capture_output=True, text=True).stdout
blocks = [b for b in out.strip().split("\n\n")]
for addr, block in zip(addrs, blocks):
    lines = block.splitlines()
    func = lines[0] if lines else "?"
    where = lines[1].split("n64recomp_body_harvest\\")[-1] if len(lines) > 1 else ""
    print(f"{addr:>10}  {counts[addr]:5d}  {func[:60]:60s} {where}")
