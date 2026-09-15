#!/usr/bin/env python3
"""Find busy-wait loops that the runtime's scheduler cannot survive.

libultra's scheduler is preemptive: a counter interrupt fires regardless of what
the running thread is doing, so game code may spin on a flag and expect another
thread or an interrupt handler to clear it. ultramodern's is not. It reschedules
-- and delivers external events, which is the same thing here -- only inside
osSendMesg, osRecvMesg and osJamMesg. A game thread that spins without reaching
one of those stops the whole program: no other thread runs, and no VI retrace,
task completion or DMA completion is ever delivered.

Phase 04 hit this at func_8008F344 and found it by bisecting a call chain one
function at a time over many build cycles. This finds the rest in one pass.

THE TEST THAT MATTERS. The obvious rule -- "a loop containing no jal" -- is
wrong, and wrong in the way that matters: func_8008F344's loop does contain a
call, to a three-instruction leaf that reads one byte. What makes a loop
dangerous is not whether it calls anything but whether anything it calls can
*reach* a yield point. So this builds the call graph, marks every function that
can reach osSendMesg, osRecvMesg or osJamMesg through any depth of calls, and
then reports loops none of whose callees are in that set.

Two deliberate choices about what to exclude:

  * A loop containing an indirect call (jalr) is skipped. Where it goes is not
    knowable from the disassembly, so reporting it would be a guess, and this is
    meant to produce a list worth reading rather than a long one.
  * A loop is only reported if it reads a global it does not also write. That is
    what separates "waiting for somebody else to change this" from an ordinary
    copy, compare or checksum loop, which are numerous and terminate on their
    own. The read may be inside a callee rather than the loop body -- which is
    exactly the shape of the known case, where the loop calls a leaf that reads
    one byte -- so reads and writes are propagated through non-yielding calls.

  * Results are ranked by loop length, shortest first. A loop that waits on
    another thread is almost always a handful of instructions; a long one is
    usually doing work.

  * THE TEST THAT MAKES THIS USABLE: the exit condition must not depend on
    anything the loop itself advances. Without it the scan reports 362
    candidates of which about six are real -- every linked-list walk and every
    bounded clear qualifies, because they read a global and never write it.
    A traversal computes its branch from a register it carries across the back
    edge (`lw $s0, 0x14($s0)`); a counted loop does the same through `addiu`.
    A genuine wait does not: it recomputes a fixed address from lui/%lo, or
    tests the result of a call, and so has nothing loop-carried in the chain
    the branch depends on. Checking that turns the list from noise into six
    entries, all real.

This reports candidates, not verdicts: whether a candidate actually deadlocks
depends on who clears the flag and whether they ever get to run, which only
running the program can settle.

Usage:  python tools/find_spin_loops.py [--asm asm] [--all]
"""
import argparse
import collections
import pathlib
import re
import sys

REG = re.compile(r"\$([a-z0-9]+)")
STORE_MNEM = re.compile(r"^s[bhwd]$|^swl$|^swr$|^sc$|^sdc1$|^swc1$")
BRANCH_MNEM = re.compile(r"^(b\w*|j|jr|jal|jalr)$")

INSN = re.compile(r"/\*\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+\*/\s+(\S+)\s*(.*)")
GLABEL = re.compile(r"^glabel (\S+)")
LABEL = re.compile(r"^\s*\.(L[0-9A-Fa-f]+):")
BRANCH = re.compile(r"^(b\w*|j)$")
LOAD = re.compile(r"^l[bhwd]u?$|^lwl$|^lwr$|^ll$")
STORE = re.compile(r"^s[bhwd]$|^swl$|^swr$|^sc$|^swc1$|^sdc1$|^swc2$")
LO_REF = re.compile(r"%lo\(([A-Za-z_][A-Za-z0-9_]*)\)")
TARGET = re.compile(r"\.(L[0-9A-Fa-f]+)\s*$")
# A loop may also branch to the function's own entry label rather than to a
# local one. func_8008FAF8 is exactly that shape -- two waits whose branches
# both target `func_8008FAF8` -- and looking only for ".L" targets missed it,
# which cost a day of bisection. Treat a branch to the enclosing function as a
# branch to its first instruction.
SELF_TARGET = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*$")
JAL_TARGET = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*$")

# The three routines that call check_running_queue and drain the external
# message queue. Reaching any of them is what makes a loop safe.
YIELD_POINTS = {"osSendMesg", "osRecvMesg", "osJamMesg", "osYieldThread", "osStopThread"}


def def_use(mnem, ops):
    """(defined register, [used registers]) for one instruction.

    Deliberately approximate. Stores and branches define nothing; a call is
    treated as defining the return registers, which is what lets a loop whose
    branch tests a callee's result come out clean. Everything else defines its
    first register operand, which is true of the arithmetic, logic, load and
    lui forms that appear in these loops.
    """
    regs = REG.findall(ops)
    if mnem in ("jal", "jalr"):
        return "v0", regs
    if STORE_MNEM.match(mnem) or BRANCH_MNEM.match(mnem) or mnem == "nop":
        return None, regs
    if not regs:
        return None, []
    return regs[0], regs[1:]


def loop_makes_progress(span):
    """True if the loop's exit condition depends on something it advances.

    Two steps. First find the loop-carried registers: those read within the
    body before anything in the body writes them, and written somewhere in it
    -- their value at the top of an iteration came from the previous one.
    Then walk the branch's tested registers backwards through the body to the
    registers they depend on. If the two sets meet, the loop is making its own
    progress and is a traversal or a count, not a wait.
    """
    written = set()
    loop_carried = set()
    for _, mnem, ops in span:
        d, uses = def_use(mnem, ops)
        for u in uses:
            if u not in written and u != "zero":
                loop_carried.add(u)
        if d:
            written.add(d)
    loop_carried &= written

    # The terminating branch is the last real instruction before the delay slot.
    branch = None
    for entry in reversed(span):
        if BRANCH_MNEM.match(entry[1]):
            branch = entry
            break
    if branch is None:
        return True
    _, uses = def_use(branch[1], branch[2])
    deps = {u for u in uses if u != "zero"}
    if not deps:
        # Nothing to analyse: an unconditional `j` back edge whose real test is
        # a forward branch elsewhere, or a floating-point condition branch,
        # which carries its predicate in a condition flag rather than a
        # register. Both shapes are overwhelmingly counted loops here, so say
        # "makes progress" rather than emit a candidate nothing can judge.
        return True

    # Walk backwards, adding what each definition of a dependency depends on.
    for _ in range(len(span)):
        grew = False
        for _, mnem, ops in span:
            d, uses2 = def_use(mnem, ops)
            if d in deps:
                for u in uses2:
                    if u != "zero" and u not in deps:
                        deps.add(u)
                        grew = True
        if not grew:
            break

    return bool(deps & loop_carried)


def parse(path):
    """-> list of (func, [(vram, mnemonic, operands)]), and label positions."""
    out = []
    func, body, labels = None, [], {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        g = GLABEL.match(line)
        if g:
            if func is not None:
                out.append((func, body, labels))
            func, body, labels = g.group(1), [], {}
            continue
        lm = LABEL.match(line)
        if lm:
            labels[lm.group(1)] = len(body)
            continue
        m = INSN.search(line)
        if m and func is not None:
            body.append((int(m.group(1), 16), m.group(2), m.group(3)))
    if func is not None:
        out.append((func, body, labels))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--asm", default="asm")
    ap.add_argument("--max-len", type=int, default=12,
                    help="only show loops of at most this many instructions")
    ap.add_argument("--all", action="store_true",
                    help="also list loops that can reach a yield point")
    args = ap.parse_args()

    root = pathlib.Path(args.asm)
    if not root.is_dir():
        raise SystemExit(f"{root} not found -- run scripts/split-rom.sh first")

    funcs = []
    for path in sorted(root.rglob("*.s")):
        funcs.extend(parse(path))

    # Call graph, then the set of functions that can reach a yield point.
    calls = {}
    for func, body, _ in funcs:
        targets = set()
        for _, mnem, ops in body:
            if mnem == "jal":
                t = JAL_TARGET.match(ops.strip())
                if t:
                    targets.add(t.group(1))
        calls[func] = targets

    # Globals each function reads and writes directly.
    reads, writes = {}, {}
    for func, body, _ in funcs:
        r, w = set(), set()
        for _, mnem, ops in body:
            for name in LO_REF.findall(ops):
                # A %lo on any instruction counts as touching the global, not
                # just on a load. MIPS materialises an address with lui/addiu
                # and then dereferences a register, so the load itself carries
                # no %lo -- which is how func_8008FAF8's two waits were missed.
                if STORE.match(mnem):
                    w.add(name)
                else:
                    r.add(name)
        reads[func], writes[func] = r, w

    yields = set(YIELD_POINTS)
    changed = True
    while changed:
        changed = False
        for func, targets in calls.items():
            if func not in yields and (targets & yields):
                yields.add(func)
                changed = True

    # Propagate reads and writes through calls that cannot yield. The known case
    # spins on a byte read by a three-instruction leaf, so a body-only view of
    # what a loop touches misses precisely the loops worth finding.
    changed = True
    while changed:
        changed = False
        for func, targets in calls.items():
            for t in targets:
                if t in yields or t not in reads:
                    continue
                if not reads[t] <= reads.setdefault(func, set()):
                    reads[func] |= reads[t]
                    changed = True
                if not writes[t] <= writes.setdefault(func, set()):
                    writes[func] |= writes[t]
                    changed = True

    rows = []
    for func, body, labels in funcs:
        for i, (vram, mnem, ops) in enumerate(body):
            if not BRANCH.match(mnem):
                continue
            t = TARGET.search(ops)
            if t and t.group(1) in labels:
                start = labels[t.group(1)]
            else:
                st = SELF_TARGET.search(ops)
                if not st or st.group(1) != func:
                    continue
                start = 0
            if start >= i:
                continue
            span = body[start:i + 2]              # include the delay slot
            if any(s[1] == "jalr" for s in span):
                continue                          # target unknowable
            callees = {JAL_TARGET.match(s[2].strip()).group(1)
                       for s in span
                       if s[1] == "jal" and JAL_TARGET.match(s[2].strip())}
            can_yield = bool(callees & yields)
            if can_yield and not args.all:
                continue
            loads, stores = set(), set()
            for _, sm, so in span:
                for name in LO_REF.findall(so):
                    if STORE.match(sm):
                        stores.add(name)
                    else:
                        loads.add(name)
            for c in callees:
                loads |= reads.get(c, set())
                stores |= writes.get(c, set())
            waited = loads - stores
            if not waited:
                continue
            if not args.all and loop_makes_progress(span):
                continue
            rows.append((len(span), func, body[start][0], vram, sorted(waited),
                         sorted(callees), can_yield))

    rows.sort()

    print(f"functions that can reach a yield point : {len(yields)}")
    print(f"call-free loops waiting on a global    : {len(rows)}\n")
    print(f"{'len':>4} {'function':<22} {'loop':<10} {'branch':<10} {'calls':<22} waits on")
    for n, func, start, branch, waited, callees, can_yield in rows:
        if not args.all and n > args.max_len:
            continue
        mark = " (can yield)" if can_yield else ""
        print(f"{n:>4} {func:<22} {start:08X}   {branch:08X}   "
              f"{','.join(callees)[:21]:<22} {', '.join(waited)[:60]}{mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
