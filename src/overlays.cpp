// Section registration and the overlay loader wrapper.
//
// This is the one translation unit that includes the generated section tables
// (their symbols are `static`).
//
// Body Harvest keeps its resident code in `core` (ROM 0x1000, linked at
// 0x80000400) and swaps eight overlays in and out of two windows: the gameplay
// overlays frontend / outside / inside at 0x80070270 and the five level overlays
// at 0x802D4CD0 (docs/GAME-INTERNALS.md). Every call is resolved by address
// (use_lookup_for_all_function_calls), so librecomp has to be told which overlay
// occupies each window *now*.
//
// The lowest point every code load passes through is the game's loader,
// func_8000FFC0_10BC0(queue, dest, rom, size) (decomp core/loader.c): it DMAs the
// range in 0x800-byte pieces, so a hook on osPiStartDma -- Wave Race 64's choice --
// would see pieces, never a whole overlay (docs/findings/phase-00.md). The loader
// is wrapped, not replaced: the wrapper updates librecomp's function map, then
// runs the recompiled original, which copies the bytes into RDRAM as the game
// expects (its data lives there even though its code never runs from there).
//
// Modelled on Wave Race 64's src/overlays.cpp (runtime functions, resident
// registration, hook installed last) and Hybrid Heaven's src/sections.cpp
// (eviction by overlap, announce at the game's own loader).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"

#include "recomp_overlays.inl"
#include "runtime_funcs.inl"

#include "bh/callbacks.h"

extern "C" void func_8000FFC0_10BC0(uint8_t* rdram, recomp_context* ctx);
extern "C" void unload_overlay_by_id(uint32_t id);
extern "C" void load_overlay_by_id(uint32_t id, uint32_t ram_addr);

namespace {

constexpr uint32_t kLoaderAddress = 0x8000FFC0;

bool env_set(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && *v != '0';
}

std::mutex g_load_mutex;
// Overlay id (the order of recomp/overlays.txt, which is the order of
// overlay_sections_by_index) -> address it is loaded at, or 0.
std::vector<uint32_t> g_loaded_at;

const SectionTableEntry& overlay_section(size_t id) {
    return section_table[overlay_sections_by_index[id]];
}

const char* overlay_name(size_t id) {
    static const char* const kNames[] = {
        "gameplay_frontend", "gameplay_outside", "gameplay_inside",
        "level_greece", "level_java", "level_america", "level_siberia", "level_comet",
    };
    return id < sizeof(kNames) / sizeof(kNames[0]) ? kNames[id] : "?";
}

// Called with the loader's arguments before the bytes move. A load whose ROM
// range starts exactly at an overlay's ROM address is that overlay; anything else
// is data (heightmaps, textures, text) and needs nothing from librecomp.
void announce_load(uint32_t dest, uint32_t rom, uint32_t size) {
    const uint32_t rom_offset = rom & 0x0FFFFFFFu;
    std::lock_guard<std::mutex> lock(g_load_mutex);
    for (size_t id = 0; id < g_loaded_at.size(); ++id) {
        const SectionTableEntry& section = overlay_section(id);
        if (section.rom_addr != rom_offset) {
            continue;
        }
        if (dest != section.ram_addr || size < section.size) {
            // The recompiled code carries its link address in every pointer it
            // builds; loaded anywhere else (or only in part) it would be wrong in
            // ways that surface far from here, so say so loudly and at once.
            std::fprintf(stderr, "[bh] overlay %s loaded at 0x%08X (0x%X bytes), but it is linked at"
                                 " 0x%08X (0x%X bytes) -- registering at the link address anyway\n",
                         overlay_name(id), dest, size, section.ram_addr, section.size);
            std::fflush(stderr);
        }
        const uint32_t lo = section.ram_addr;
        const uint32_t hi = section.ram_addr + section.size;
        size_t evicted = 0;
        for (size_t other = 0; other < g_loaded_at.size(); ++other) {
            if (g_loaded_at[other] == 0) {
                continue;
            }
            const uint32_t olo = g_loaded_at[other];
            const uint32_t ohi = olo + overlay_section(other).size;
            if (olo < hi && lo < ohi) {
                // Evict by id rather than with unload_overlays(ram, size), which
                // exits on a partial overlap -- and the gameplay overlays differ in
                // size. unload_overlay_by_id erases only the entries it loaded.
                unload_overlay_by_id(static_cast<uint32_t>(other));
                g_loaded_at[other] = 0;
                ++evicted;
            }
        }
        load_overlay_by_id(static_cast<uint32_t>(id), section.ram_addr);
        g_loaded_at[id] = section.ram_addr;
        // Wrappers on functions inside this overlay (src/widescreen.cpp) replace
        // the entries the load just wrote.
        bh::overlay_loaded(id);

        static const bool trace = env_set("BH_DEBUG_LOADS");
        if (trace) {
            std::fprintf(stderr, "[bh-load] overlay %-17s -> 0x%08X (rom 0x%06X, 0x%06X bytes, %zu evicted)\n",
                         overlay_name(id), section.ram_addr, rom_offset, size, evicted);
            std::fflush(stderr);
        }
        return;
    }
    static const bool trace_data = env_set("BH_DEBUG_LOADS");
    if (trace_data) {
        std::fprintf(stderr, "[bh-load] data 0x%06X -> 0x%08X (0x%X bytes)\n", rom_offset, dest, size);
        std::fflush(stderr);
    }
}

// Wraps the game's loader: register the overlay's functions, then load it.
void loader_hook(uint8_t* rdram, recomp_context* ctx) {
    // Arguments are read before calling through: the callee owns the context.
    const uint32_t dest = static_cast<uint32_t>(ctx->r5);
    const uint32_t rom = static_cast<uint32_t>(ctx->r6);
    const uint32_t size = static_cast<uint32_t>(ctx->r7);
    announce_load(dest, rom, size);
    func_8000FFC0_10BC0(rdram, ctx);
}

}  // namespace

namespace bh {

void register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections{};
    sections.code_sections = section_table;
    sections.num_code_sections = ARRLEN(section_table);
    sections.total_num_sections = num_sections;

    recomp::overlays::overlays_by_index_t overlays{};
    overlays.table = overlay_sections_by_index;
    overlays.len = ARRLEN(overlay_sections_by_index);

    recomp::overlays::register_overlays(sections, overlays);
    g_loaded_at.assign(ARRLEN(overlay_sections_by_index), 0);
}

void register_runtime_functions() {
    // Must run from on_init: init_overlays() begins with func_map.clear().
    //
    // librecomp's boot load maps ROM 0x1000 + 1 MB to the entry point. That covers
    // core, and also the frontend overlay (ROM 0x40720, entirely inside the
    // megabyte), which it registers at 0x8003FB20 -- core's bss, where nothing
    // is called -- *and* records as loaded there. Left alone, the first real
    // frontend load would be offset by that bogus address (load_overlay_by_id
    // adds a previous address to the new one). Unload every overlay first.
    for (size_t id = 0; id < ARRLEN(overlay_sections_by_index); ++id) {
        unload_overlay_by_id(static_cast<uint32_t>(id));
    }

    // With every call resolved by address, the libultra functions the runtime
    // provides need their cartridge addresses too (playbook 04).
    for (const auto& entry : runtime_provided_funcs) {
        recomp::overlays::add_loaded_function(static_cast<int32_t>(entry.ram_addr), entry.func);
    }

    // Every function of every section that does not share its address. Only core
    // qualifies; the boot load already registered it, and doing it again costs
    // nothing and does not depend on the size of the boot megabyte.
    std::unordered_map<uint32_t, int> address_uses;
    for (size_t i = 0; i < ARRLEN(section_table); ++i) {
        address_uses[section_table[i].ram_addr]++;
    }
    size_t resident = 0;
    for (size_t i = 0; i < ARRLEN(section_table); ++i) {
        const SectionTableEntry& section = section_table[i];
        if (address_uses[section.ram_addr] > 1) {
            continue;
        }
        for (size_t f = 0; f < section.num_funcs; ++f) {
            recomp::overlays::add_loaded_function(
                static_cast<int32_t>(section.ram_addr + section.funcs[f].offset), section.funcs[f].func);
            ++resident;
        }
    }

    // Wrappers around runtime-provided libultra (src/libultra_glue.cpp), after the
    // table above so they replace its entries.
    install_libultra_glue();
    // Game-side picture changes (src/widescreen.cpp).
    install_widescreen();
    install_frame_stats();

    // Last, so nothing above overwrites it.
    recomp::overlays::add_loaded_function(static_cast<int32_t>(kLoaderAddress), loader_hook);

    std::fprintf(stderr, "[bh] registered %zu runtime-provided and %zu resident functions;"
                         " loader wrapped at 0x%08X\n",
                 sizeof(runtime_provided_funcs) / sizeof(runtime_provided_funcs[0]), resident,
                 kLoaderAddress);
    std::fflush(stderr);
}

size_t code_section_count() {
    return ARRLEN(section_table);
}

}  // namespace bh
