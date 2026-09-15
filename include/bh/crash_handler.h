#pragma once

#include <cstdint>

namespace bh {

// Installs a handler that reports the faulting address and owning module before
// the process dies. See src/crash_handler.cpp for why this is worth having.
void install_crash_handler();

// Reads through a stale pointer into the unmapped part of the RDRAM reservation
// (librecomp reserves 4 GB and maps only real RAM) read zeros instead of killing
// the process, logged once per page; writes there still crash. The game relies on
// this: several of its functions read through uninitialised stack variables that
// happen to hold valid addresses on the console (docs/findings/phase-05.md,
// "Entering a building"). BH_STRICT_MEMORY=1: every such access crashes.
void set_rdram_for_fault_handling(uint8_t* rdram);

// Watches a piece of work that is expected to finish quickly, and if it has not
// finished after `seconds`, reports where the calling thread is stuck. A
// recompiled microcode that spins forever is otherwise invisible: no fault, no
// output, just a thread that never comes back and a game that stops.
void watch_for_hang(const char* what, int seconds);
void watch_done();

}  // namespace bh
