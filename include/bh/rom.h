#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bh {

// Byte order of an N64 dump, identified from the first word of the header.
enum class RomFormat {
    Unknown,
    Z64,  // big endian,     0x80371240 -- the only format the toolchain accepts
    N64,  // little endian,  0x40123780
    V64,  // byte swapped,   0x37804012
};

// The fields of the 64-byte N64 cartridge header that identify a dump.
struct RomHeader {
    RomFormat   format = RomFormat::Unknown;
    uint32_t    crc1 = 0;          // header offset 0x10
    uint32_t    crc2 = 0;          // header offset 0x14
    std::string internal_name;     // header offset 0x20, 20 bytes, space padded
    std::string cartridge_id;      // header offset 0x3C, 2 bytes  ("BH" for Body Harvest)
    char        region = '\0';     // header offset 0x3E          ('E' = USA)
    uint8_t     revision = 0;      // header offset 0x3F
    uint64_t    size_bytes = 0;
};

// What the recompilation pipeline is pinned to. Every overlay address and every
// generated function assumes this exact dump:
//
//   Body Harvest (USA), NBHE, version 0
//   sha1  bbb6666f5014a473747ee4145f036d9fb25d7348
//
// It is the revision the decompilation builds byte-for-byte. Europe (NBHP) is
// a different build with different addresses. docs/findings/phase-00.md has the
// survey.
inline constexpr char        kTargetCartridgeId[] = "BH";
inline constexpr char        kTargetRegion  = 'E';
inline constexpr uint8_t     kTargetRevision = 0;
inline constexpr uint64_t    kTargetSizeBytes = 12u * 1024u * 1024u;

// CRC1/CRC2 from the header of the pinned dump. Setting both to zero disables
// the check and leaves only structural checks.
inline constexpr uint32_t    kTargetCrc1 = 0x5326696F;
inline constexpr uint32_t    kTargetCrc2 = 0xFE9A99C3;

std::string to_string(RomFormat format);

// Reads the first 64 bytes and the file size. Does not load the ROM.
bool read_header(const std::filesystem::path& path, RomHeader& out, std::string& error);

// Structural verification against the pinned target above. Populates `problems`
// with one human-readable line per mismatch; returns true when there are none.
bool verify(const RomHeader& header, std::vector<std::string>& problems);

// How librecomp and RecompFrontend identify this game and its dump.
//
// These live here rather than in main.cpp because the launcher needs them too:
// the ROM picker validates whatever file the player chooses against this hash
// (XXH3-64 of the .z64, which is what librecomp checks -- not the sha1), and
// reports which way it failed. One definition, two callers.
inline constexpr uint64_t kRomHash = 0x9949AA9BC66F33CBULL;
inline constexpr char8_t  kGameId[] = u8"bh.us.0";
inline constexpr char     kModGameId[] = "bh";
inline constexpr char     kDisplayName[] = "Body Harvest";
inline constexpr char     kInternalName[] = "BODY HARVEST";

}  // namespace bh
