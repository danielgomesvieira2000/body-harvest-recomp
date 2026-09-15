// BH_TRACE_FUNCS=0x8001B130,0x8001B180,...: print every call to those game
// addresses with its first four arguments and its return value.
//
// Reading which path the game takes beats reasoning about which one it should
// take (playbook 04, "a plausible path found by reading is not evidence"). Every
// call goes through the runtime's function lookup in this port
// (use_lookup_for_all_function_calls), so wrapping an address in the function map
// sees every caller, resident or overlay. Runtime-provided libultra functions are
// wrapped the same way, since they are registered at their cartridge addresses.
//
// BH_TRACE_LIMIT=<n> (default 40) caps the lines per address. Installed from
// on_init after every other registration (bh::install_call_traces), so nothing
// registered later replaces a wrapper. Overlay functions are replaced when their
// overlay loads, so trace resident or runtime functions.

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "recomp.h"
#include "librecomp/overlays.hpp"

extern "C" recomp_func_t* get_function(int32_t addr);

namespace {

constexpr size_t kMaxTraces = 16;

struct Trace {
    uint32_t address = 0;
    recomp_func_t* original = nullptr;
    std::atomic<int> calls{ 0 };
};

std::array<Trace, kMaxTraces> g_traces;
int g_limit = 40;

void traced(size_t slot, uint8_t* rdram, recomp_context* ctx) {
    Trace& t = g_traces[slot];
    const int n = ++t.calls;
    const bool print = n <= g_limit;
    const uint32_t a0 = static_cast<uint32_t>(ctx->r4), a1 = static_cast<uint32_t>(ctx->r5);
    const uint32_t a2 = static_cast<uint32_t>(ctx->r6), a3 = static_cast<uint32_t>(ctx->r7);
    if (print) {
        std::fprintf(stderr, "[bh-trace] #%d 0x%08X(%08X, %08X, %08X, %08X)\n", n, t.address, a0, a1, a2, a3);
        std::fflush(stderr);
    }
    t.original(rdram, ctx);
    if (print) {
        std::fprintf(stderr, "[bh-trace] #%d 0x%08X -> %08X\n", n, t.address, static_cast<uint32_t>(ctx->r2));
        std::fflush(stderr);
    }
}

template <size_t I>
void trampoline(uint8_t* rdram, recomp_context* ctx) {
    traced(I, rdram, ctx);
}

constexpr std::array<recomp_func_t*, kMaxTraces> kTrampolines = {
    trampoline<0>, trampoline<1>, trampoline<2>, trampoline<3>,
    trampoline<4>, trampoline<5>, trampoline<6>, trampoline<7>,
    trampoline<8>, trampoline<9>, trampoline<10>, trampoline<11>,
    trampoline<12>, trampoline<13>, trampoline<14>, trampoline<15>,
};

}  // namespace

namespace bh {

void install_call_traces() {
    const char* list = std::getenv("BH_TRACE_FUNCS");
    if (list == nullptr || *list == '\0') {
        return;
    }
    if (const char* limit = std::getenv("BH_TRACE_LIMIT")) {
        g_limit = std::atoi(limit);
    }
    std::string s{ list };
    size_t slot = 0;
    size_t pos = 0;
    while (pos < s.size() && slot < kMaxTraces) {
        size_t comma = s.find(',', pos);
        const std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? s.size() : comma + 1;
        const uint32_t addr = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
        if (addr == 0) {
            continue;
        }
        g_traces[slot].address = addr;
        g_traces[slot].original = get_function(static_cast<int32_t>(addr));
        recomp::overlays::add_loaded_function(static_cast<int32_t>(addr), kTrampolines[slot]);
        std::fprintf(stderr, "[bh-trace] tracing 0x%08X (limit %d)\n", addr, g_limit);
        ++slot;
    }
    std::fflush(stderr);
}

}  // namespace bh
