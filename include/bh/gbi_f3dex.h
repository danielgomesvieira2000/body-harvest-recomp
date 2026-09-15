#pragma once

// F3DEX 1.x command numbers and field layouts, as Body Harvest's graphics
// microcodes (F3DEX 1.21 and L3DEX 1.21) use them. Read from RT64's own
// decoders (lib/RT64/src/gbi/rt64_gbi_f3d.cpp, rt64_gbi_f3dex.cpp,
// rt64_gbi_rdp.cpp), not recalled: Hybrid Heaven's walker is F3DEX2, whose
// numbering and several layouts differ (playbook 08, "F3DEX2 is not Fast3D").
//
// RDRAM as N64Recomp keeps it: every 32-bit word in host order at its physical
// offset, so a command's words are read with a plain memcpy.

#include <cstdint>
#include <cstring>

namespace bh::gbi {

// RSP (F3D numbering, F3DEX additions)
constexpr uint8_t kSpNoop = 0x00;          // also RT64's extended-GBI hook on F3D
constexpr uint8_t kMtx = 0x01;             // params in bits 16-23: PROJECTION 0x01, LOAD 0x02, PUSH 0x04
constexpr uint8_t kMoveMem = 0x03;         // type in bits 16-23; G_MV_VIEWPORT 0x80
constexpr uint8_t kVtx = 0x04;             // count bits 10-15, first index bits 17-23, w1 = address
constexpr uint8_t kDl = 0x06;              // bit 16 set = branch (no push)
constexpr uint8_t kLoadUcode = 0xAF;
constexpr uint8_t kBranchZ = 0xB0;
constexpr uint8_t kTri2 = 0xB1;            // indices (x2) at bits 17/9/1 of w0 and of w1
constexpr uint8_t kModifyVtx = 0xB2;
constexpr uint8_t kRdpHalf2 = 0xB3;
constexpr uint8_t kRdpHalf1 = 0xB4;
constexpr uint8_t kQuad = 0xB5;            // indices (x2) at bits 25/17/9/1 of w1; L3DEX's line
constexpr uint8_t kClearGeometryMode = 0xB6;
constexpr uint8_t kSetGeometryMode = 0xB7;
constexpr uint8_t kEndDl = 0xB8;
constexpr uint8_t kSetOtherModeL = 0xB9;
constexpr uint8_t kSetOtherModeH = 0xBA;
constexpr uint8_t kTexture = 0xBB;
constexpr uint8_t kMoveWord = 0xBC;        // type in bits 0-7 (G_MW_SEGMENT 0x06), segment in bits 10-13
constexpr uint8_t kPopMtx = 0xBD;
constexpr uint8_t kCullDl = 0xBE;
constexpr uint8_t kTri1 = 0xBF;            // indices (x2) at bits 17/9/1 of w1

// RDP
constexpr uint8_t kRdpNoop = 0xC0;         // G_NOOP
constexpr uint8_t kTexRect = 0xE4;         // HLE consumes the next two commands (s,t then dsdx,dtdy)
constexpr uint8_t kTexRectFlip = 0xE5;
constexpr uint8_t kSetScissor = 0xED;
constexpr uint8_t kFillRect = 0xF6;
constexpr uint8_t kSetFillColor = 0xF7;
constexpr uint8_t kSetTImg = 0xFD;
constexpr uint8_t kSetCImg = 0xFF;

constexpr uint8_t kMwSegment = 0x06;
constexpr uint8_t kMvViewport = 0x80;
constexpr uint8_t kMtxProjection = 0x01;
constexpr uint8_t kMtxLoad = 0x02;
constexpr uint8_t kMtxPush = 0x04;

// Whether `op` is a command either microcode defines. Anything else means a walk
// has left the list (an unresolved segment), and it should stop.
inline bool known(uint8_t op) {
    if (op <= 0x09) return true;                    // RSP DMA commands
    if (op >= 0xAF && op <= 0xBF) return true;      // RSP immediate commands
    if (op >= 0xC0) return true;                    // RDP commands and G_NOOP
    return false;
}

inline uint32_t read_word(const uint8_t* rdram, uint32_t phys) {
    uint32_t v;
    std::memcpy(&v, rdram + (phys & 0x7FFFFC), sizeof v);
    return v;
}

}  // namespace bh::gbi
