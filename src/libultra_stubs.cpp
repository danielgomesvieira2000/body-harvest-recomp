// libultra functions N64Recomp does not translate, that recompiled game code still
// calls, and that librecomp does not implement.
//
// The link names them: an `ignored` libultra name becomes a call to <name>_recomp
// with no body anywhere. Each is answered with what the cartridge's own routine
// does, read from its instructions (docs/findings/phase-02.md), not with a
// placeholder.

#include <cmath>
#include <cstdint>

#include "recomp.h"

namespace {

// Return a 64-bit result the way libultra's ll routines do: high word in $v0,
// low word in $v1, each sign-extended as the recompiler keeps every GPR.
void return_64(recomp_context* ctx, uint64_t value) {
    ctx->r2 = static_cast<int32_t>(value >> 32);
    ctx->r3 = static_cast<int32_t>(value);
}

// cvt.l with the FPCSR rounding mode set to 1 (round toward zero), as the
// routines at 0x80034AB8 and 0x80034B58 do; invalid (NaN, out of range) is -1.
bool to_s64(double value, int64_t* out) {
    if (std::isnan(value) || value >= 9223372036854775808.0 || value < -9223372036854775808.0) {
        return false;
    }
    *out = static_cast<int64_t>(value);   // C++ truncates toward zero
    return true;
}

// __d_to_ull / __f_to_ull, instruction for instruction:
//   convert; if invalid: subtract 2^63, convert again; invalid -> -1, else OR 2^63
//   if valid but negative -> -1
uint64_t to_u64(double value) {
    int64_t s = 0;
    if (!to_s64(value, &s)) {
        if (!to_s64(value - 9223372036854775808.0, &s)) {
            return UINT64_MAX;
        }
        return static_cast<uint64_t>(s) | 0x8000000000000000ull;
    }
    if (s < 0) {
        return UINT64_MAX;
    }
    return static_cast<uint64_t>(s);
}

}  // namespace

extern "C" {

// 0x80034A80: trunc.l.d $f4, $f12; result in $v0:$v1.
void __d_to_ll_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    int64_t s = 0;
    return_64(ctx, to_s64(ctx->f12.d, &s) ? static_cast<uint64_t>(s) : 0x8000000000000000ull);
}

// 0x80034AB8. Called by the game (0x80001454, files 10 and 24).
void __d_to_ull_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    return_64(ctx, to_u64(ctx->f12.d));
}

// 0x80034B58.
void __f_to_ull_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    return_64(ctx, to_u64(static_cast<double>(ctx->f12.fl)));
}

// 0x80034BF4: dmtc1 of $a0:$a1, cvt.d.l into $f0.
void __ll_to_d_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const int64_t a = static_cast<int64_t>((static_cast<uint64_t>(ctx->r4) << 32) |
                                           (static_cast<uint64_t>(ctx->r5) & 0xFFFFFFFFu));
    ctx->f0.d = static_cast<double>(a);
}

}  // extern "C"
