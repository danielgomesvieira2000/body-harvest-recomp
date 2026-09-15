"""Stop ultramodern's timers firing early on Windows.

The timer thread waits for the next timer with moodycamel's
wait_dequeue_timed(duration). On Windows that ends in
WaitForSingleObject(sema, (unsigned long)(usecs / 1000)): the wait is truncated to
whole milliseconds. A timer due in under a millisecond fires at once, and every
other one up to a millisecond early. Linux waits in nanoseconds, so the two
platforms paced differently.

Measured in Body Harvest (docs/findings/phase-05.md, "The controller loop"): the
controller thread blocks on a 1 ms timer each pass to stand in for the SI
transfer, and BH_SI_TRACE=1 showed 29,000 passes a second and 13 us per block. At
5 ms: 220 passes a second and 4,300 us per block -- a millisecond short. The game
counts its rumble timings in passes, so rumble ran 25 times too fast.

The patch rounds the wait up to the next whole millisecond. A timer then fires at
most a millisecond late, never early and never at once, which is what a game
waiting on a timer can rely on.

Scripted and idempotent because it patches a submodule. Run from the repository root:
    python tools/patch_runtime_timer.py
"""

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "lib" / "N64ModernRuntime" / "ultramodern" / "src" / "timer.cpp"

MARKER = "bh: round the wait up"

ANCHOR = """        // Wait for either the duration to complete or a new action to come through
        if (wait_duration.count() >= 0 && timer_context.action_queue.wait_dequeue_timed(cur_action, wait_duration)) {"""

REPLACEMENT = """        // Wait for either the duration to complete or a new action to come through
        // (bh: round the wait up to a whole millisecond -- on Windows the semaphore wait is
        // truncated to milliseconds, so a shorter or fractional wait fired the timer early;
        // tools/patch_runtime_timer.py)
        if (wait_duration.count() >= 0 && timer_context.action_queue.wait_dequeue_timed(cur_action,
                std::chrono::ceil<std::chrono::milliseconds>(wait_duration))) {"""


def main():
    if not TARGET.exists():
        sys.exit(f"missing {TARGET}\nRun: git submodule update --init --recursive")
    text = TARGET.read_text()
    if MARKER in text:
        print(f"  {TARGET.name} already patched")
        return
    if ANCHOR not in text:
        sys.exit(f"anchor not found in {TARGET}; upstream has changed and this patch needs revisiting")
    TARGET.write_text(text.replace(ANCHOR, REPLACEMENT, 1))
    print(f"  {TARGET.name} patched")


if __name__ == "__main__":
    main()
