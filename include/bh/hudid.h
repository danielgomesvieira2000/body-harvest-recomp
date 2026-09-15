#pragma once

// HUD element identities, shared by the F1 panel's feed (src/dlcensus.cpp) and
// the rewriter (src/hudrewrite.cpp) so both name an element the same way.
//
// Copied from Hybrid Heaven's include/hh/hudid.h, where an address alone proved
// not to be an identity (segments remapped per scene, heap addresses reused, a
// colour naming every rectangle of that colour). Each identity carries what is
// actually drawn:
//
//   tex:<image address>#<hash of the image's first 64 bytes>
//   dl:<list address>#<hash of the list's first 16 commands>
//   fill:<colour>@<ulx>,<uly>,<lrx>,<lry>   (320x240 pixels)
//
// Full-frame fill rectangles are the clears and have no identity at all.

#include <cstdint>
#include <cstdio>
#include <string>

#include "bh/gbi_f3dex.h"

namespace bh::hudid {

inline uint32_t fnv(uint32_t h, uint32_t word) {
    for (int i = 0; i < 4; ++i) {
        h ^= (word >> (8 * i)) & 0xFF;
        h *= 16777619u;
    }
    return h;
}

inline std::string texture(const uint8_t* rdram, uint32_t address, uint32_t phys) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 16; ++i) h = fnv(h, bh::gbi::read_word(rdram, phys + 4 * i));
    char buf[40];
    std::snprintf(buf, sizeof buf, "tex:0x%08x#%08x", address, h);
    return buf;
}

inline std::string list(const uint8_t* rdram, uint32_t address, uint32_t phys) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t w0 = bh::gbi::read_word(rdram, phys + 8 * i);
        h = fnv(fnv(h, w0), bh::gbi::read_word(rdram, phys + 8 * i + 4));
        if ((w0 >> 24) == bh::gbi::kEndDl) break;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "dl:0x%08x#%08x", address, h);
    return buf;
}

// Coordinates in 320x240 pixels. Empty for a full-frame clear.
inline std::string fill(uint32_t colour, int ulx, int uly, int lrx, int lry) {
    if (ulx <= 0 && uly <= 0 && lrx >= 319 && lry >= 239) return std::string();
    char buf[48];
    std::snprintf(buf, sizeof buf, "fill:0x%08x@%d,%d,%d,%d", colour, ulx, uly, lrx, lry);
    return buf;
}

}  // namespace bh::hudid
