// libultra state the runtime's reimplementations do not create, but the game's
// own (recompiled) libultra code relies on.
//
// The runtime owns a libultra function by name (playbook 03, "naming is the
// mechanism"): the cartridge's body is never run. When that body had a side
// effect some *other*, unnamed cartridge routine depends on, the side effect is
// lost and the dependent routine hangs far from the cause.
//
// Registered at cartridge addresses from on_init, after the runtime function
// table (bh::install_libultra_glue, called from src/overlays.cpp).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultra64.h"

extern "C" void osContInit_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void osCreateMesgQueue_recomp(uint8_t* rdram, recomp_context* ctx);

namespace {

// ---- osContInit: the EEPROM timer queue ------------------------------------
//
// Symptom: two display lists at boot, then nothing; every game thread parked in
// osRecvMesg (BH_SAMPLE=1). BH_TRACE_FUNCS showed the boot thread entering the
// game's hand-written osEepromLongRead (func_8001D5A0_1E1A0), doing one
// osEepromRead, calling osSetTimer and never returning.
//
// Cause: that routine waits ~15 ms between blocks on a timer whose message goes
// to __osEepromTimerQ (0x8006CA28). libultra's osContInit creates that queue --
// the cartridge's own body does it at 0x8001CEDC:
//     osCreateMesgQueue(&__osEepromTimerQ, &__osEepromTimerMsg, 1)
// -- but osContInit is reimplemented by the runtime, whose version does not. The
// queue stays zeroed (the game bzeroes RAM above 0x8003FB20 at boot), holds no
// slot, and the timer's message never lands.
//
// Fix: run the runtime's osContInit, then create the queue exactly as the
// cartridge would. The game's 15 ms pacing of EEPROM blocks is kept.
constexpr uint32_t kOsContInit = 0x8001CD00;
constexpr uint32_t kEepromTimerQ = 0x8006CA28;
constexpr uint32_t kEepromTimerMsg = 0x8006CA40;

void os_cont_init_glue(uint8_t* rdram, recomp_context* ctx) {
    osContInit_recomp(rdram, ctx);
    const gpr result = ctx->r2;
    recomp_context q = *ctx;
    q.r4 = static_cast<gpr>(static_cast<int32_t>(kEepromTimerQ));
    q.r5 = static_cast<gpr>(static_cast<int32_t>(kEepromTimerMsg));
    q.r6 = 1;
    osCreateMesgQueue_recomp(rdram, &q);
    ctx->r2 = result;
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "[bh] osContInit: created __osEepromTimerQ (0x%08X) as the cartridge's osContInit does\n",
                     kEepromTimerQ);
        std::fflush(stderr);
    }
}

// ---- osContStartReadData: a controller read takes time ----------------------
//
// Symptom (after the fix above): still two display lists, then nothing.
// BH_TRACE_FUNCS on osSendMesg/osRecvMesg showed the boot thread's hand-off to the
// game thread (osSendMesg(D_8006A8F0) after the save is read) succeeding, and the
// game thread's osRecvMesg on that queue never returning.
//
// Cause: the controller thread (func_80002EF8_3AF8, priority 5) is a loop --
//     osContStartReadData(q); for (;;) { osRecvMesg(q, BLOCK); ...; osContStartReadData(q); }
// -- and the game thread it should yield to runs at priority 4. On hardware the
// SI transfer completes a moment later, so each osRecvMesg blocks and lower
// threads run. The runtime completes the transfer on the spot: the SI message is
// already queued when osRecvMesg looks, the receive never blocks, and ultramodern's
// cooperative scheduler never reaches a lower-priority thread (playbook 05,
// "threads and cooperative scheduling").
//
// Fix: after the runtime starts the read, block the calling thread on a timer for
// the transfer's duration, as the SI interrupt's latency would. The default is
// 3 ms: ares estimates a PIF controller read at 13,600 cycles plus 22,000 per
// connected pad and 18,000 per empty port (four polled here), times three, at
// 93.75 MHz -- ~2.9 ms -- plus ~0.13 ms for the write (docs/findings/phase-05.md,
// "The controller loop"). The game counts its rumble timings in these passes, so
// the rate is not cosmetic: at the earlier 1 ms, which Windows' timer
// truncation turned into no wait at all, the loop ran ~29,000 passes a second and
// rumble 25 times too fast (tools/patch_runtime_timer.py).
// BH_SI_LATENCY_MS=<n> changes it (0 restores the immediate completion).
// BH_SI_TRACE=1 logs reads a second and the time each actually blocked.
//
// The timer and its queue live in scratch RDRAM above the game's 4 MB (inferred
// free, docs/findings/phase-00.md), below the audio command-list copy at
// 0x807F0000.
constexpr uint32_t kOsContStartReadData = 0x8001D6E0;
constexpr uint32_t kSiPaceTimer = 0x807EF000;   // OSTimer, 0x20 bytes
constexpr uint32_t kSiPaceQueue = 0x807EF040;   // OSMesgQueue, 0x18 bytes
constexpr uint32_t kSiPaceMsg   = 0x807EF060;   // one OSMesg

uint32_t si_latency_ms() {
    static const uint32_t ms = []() -> uint32_t {
        const char* v = std::getenv("BH_SI_LATENCY_MS");
        if (v == nullptr || *v == '\0') {
            return 3;
        }
        const long parsed = std::strtol(v, nullptr, 10);
        const uint32_t clamped = static_cast<uint32_t>(parsed < 0 ? 0 : (parsed > 100 ? 100 : parsed));
        std::fprintf(stderr, "[bh] BH_SI_LATENCY_MS: controller reads take %u ms (default 3)\n", clamped);
        return clamped;
    }();
    return ms;
}

void os_cont_start_read_data_glue(uint8_t* rdram, recomp_context* ctx) {
    osContStartReadData_recomp(rdram, ctx);
    const uint32_t ms = si_latency_ms();
    if (ms == 0) {
        return;
    }
    const gpr result = ctx->r2;
    static bool created = false;
    if (!created) {
        created = true;
        osCreateMesgQueue(rdram, static_cast<int32_t>(kSiPaceQueue), static_cast<int32_t>(kSiPaceMsg), 1);
    }
    constexpr uint64_t kTicksPerMs = 46875;   // ultramodern's counter rate (timer.cpp)
    const auto blocked_from = std::chrono::steady_clock::now();
    osSetTimer(rdram, static_cast<int32_t>(kSiPaceTimer), ms * kTicksPerMs, 0,
               static_cast<int32_t>(kSiPaceQueue), 0);
    osRecvMesg(rdram, static_cast<int32_t>(kSiPaceQueue), NULLPTR, OS_MESG_BLOCK);
    ctx->r2 = result;

    // BH_SI_TRACE=1: reads a second and the time each one actually blocked.
    static const bool trace = std::getenv("BH_SI_TRACE") != nullptr;
    if (trace) {
        using clock = std::chrono::steady_clock;
        static clock::time_point window = clock::now();
        static uint32_t reads = 0;
        static double blocked_us = 0, blocked_max_us = 0;
        const double us = std::chrono::duration<double, std::micro>(clock::now() - blocked_from).count();
        ++reads;
        blocked_us += us;
        blocked_max_us = std::max(blocked_max_us, us);
        if (clock::now() - window >= std::chrono::seconds(1)) {
            std::fprintf(stderr, "[bh-si] %u controller reads/s, blocked %.0f us mean, %.0f us max (latency %u ms)\n",
                         reads, blocked_us / reads, blocked_max_us, ms);
            std::fflush(stderr);
            window = clock::now();
            reads = 0;
            blocked_us = blocked_max_us = 0;
        }
    }
}

// ---- the building loader's uninitialised pointer ---------------------------
//
// Symptom: entering a building (opening a house door) crashes in
// recomp::do_rom_read. BH_DEBUG_LOADS=1: the inside overlay loads, then data
// "0x38F640 -> 0x00000001 (0x4 bytes)".
//
// Cause: the game's own code. func_800105F0_111F0 (core loader.c, a matching
// function), called by the inside overlay's loadLevel, loads four bytes into
// *sp28 and returns them -- and never sets sp28: `lw $a0, 0x28($sp)` passes
// whatever that stack slot holds as the destination. On the console the slot
// holds an address left there by an earlier call, and the four bytes land
// somewhere harmless. In the port the runtime's native libultra functions never
// write the MIPS stack, so the slot holds something else: here 1.
//
// Fix: before the call, put a pointer to four bytes of port scratch RDRAM into
// the slot its frame will use (caller's sp - 0x38 + 0x28). The value returned is
// what the console returns -- the four bytes read -- and nothing of the game's is
// overwritten. BH_NO_LOADER_SLOT=1: off.
constexpr uint32_t kBuildingLoader = 0x800105F0;
constexpr uint32_t kLoaderSlotTarget = 0x807EF068;   // scratch, 4 bytes
extern "C" void func_800105F0_111F0(uint8_t* rdram, recomp_context* ctx);

void building_loader_with_slot(uint8_t* rdram, recomp_context* ctx) {
    MEM_W(-0x38 + 0x28, ctx->r29) = static_cast<int32_t>(kLoaderSlotTarget);
    func_800105F0_111F0(rdram, ctx);
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "[bh] building loader: its uninitialised destination points at scratch 0x%08X (BH_NO_LOADER_SLOT=1: off)\n",
                     kLoaderSlotTarget);
        std::fflush(stderr);
    }
}

}  // namespace

namespace bh {

void install_libultra_glue() {
    {
        const char* v = std::getenv("BH_NO_LOADER_SLOT");
        if (!(v != nullptr && *v != '\0' && *v != '0')) {
            recomp::overlays::add_loaded_function(static_cast<int32_t>(kBuildingLoader), building_loader_with_slot);
        }
    }
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kOsContInit), os_cont_init_glue);
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kOsContStartReadData), os_cont_start_read_data_glue);
}

}  // namespace bh
