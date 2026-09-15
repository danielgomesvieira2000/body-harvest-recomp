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
// the transfer's duration, as the SI interrupt's latency would. Emulators raise
// that interrupt ~0x900 count ticks (well under a millisecond) after the transfer
// starts; the host timer's floor is about 1 ms, so that is the default.
// BH_SI_LATENCY_MS=<n> changes it (0 restores the immediate completion).
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
            return 1;
        }
        const long parsed = std::strtol(v, nullptr, 10);
        const uint32_t clamped = static_cast<uint32_t>(parsed < 0 ? 0 : (parsed > 100 ? 100 : parsed));
        std::fprintf(stderr, "[bh] BH_SI_LATENCY_MS: controller reads take %u ms (default 1)\n", clamped);
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
    osSetTimer(rdram, static_cast<int32_t>(kSiPaceTimer), ms * kTicksPerMs, 0,
               static_cast<int32_t>(kSiPaceQueue), 0);
    osRecvMesg(rdram, static_cast<int32_t>(kSiPaceQueue), NULLPTR, OS_MESG_BLOCK);
    ctx->r2 = result;
}

}  // namespace

namespace bh {

void install_libultra_glue() {
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kOsContInit), os_cont_init_glue);
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kOsContStartReadData), os_cont_start_read_data_glue);
}

}  // namespace bh
