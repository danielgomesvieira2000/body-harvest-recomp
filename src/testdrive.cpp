// Scripted input and a game-state transcript, for verifying runs.
//
// See include/bh/testdrive.h for why this exists. In short: from outside the
// process a port stuck on the title screen is indistinguishable from one that is
// running, and a run driven by hand cannot be repeated.

#include "bh/testdrive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

namespace {

// ------------------------------------------------------------ input script ---

// N64 controller button bits, as libultra defines them. Duplicated from
// callbacks.cpp deliberately: this file is a test harness and should not make
// the input path depend on it.
struct NamedButton { const char* name; uint16_t bit; };
constexpr NamedButton kButtons[] = {
    { "A",      0x8000 }, { "B",      0x4000 }, { "Z",      0x2000 },
    { "START",  0x1000 }, { "DUP",    0x0800 }, { "DDOWN",  0x0400 },
    { "DLEFT",  0x0200 }, { "DRIGHT", 0x0100 }, { "L",      0x0020 },
    { "R",      0x0010 }, { "CUP",    0x0008 }, { "CDOWN",  0x0004 },
    { "CLEFT",  0x0002 }, { "CRIGHT", 0x0001 },
};

struct ScriptEntry {
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    uint16_t buttons = 0;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    std::string note;
    bool announced = false;
    int player = 0;     // 0 for player one; a "2:" prefix on the buttons makes it 1
};

std::vector<ScriptEntry> g_script;
bool g_script_loaded = false;
bool g_script_has_player_two = false;
Uint64 g_script_start_ticks = 0;

double now_seconds() {
    if (g_script_start_ticks == 0) {
        g_script_start_ticks = SDL_GetTicks64();
    }
    return static_cast<double>(SDL_GetTicks64() - g_script_start_ticks) / 1000.0;
}

bool parse_buttons(const std::string& field, uint16_t* out) {
    *out = 0;
    if (field == "-" || field.empty()) {
        return true;
    }
    std::stringstream parts{ field };
    std::string name;
    while (std::getline(parts, name, ',')) {
        const NamedButton* found = nullptr;
        for (const NamedButton& candidate : kButtons) {
            if (name == candidate.name) {
                found = &candidate;
                break;
            }
        }
        if (found == nullptr) {
            std::fprintf(stderr, "[bh] input script: unknown button '%s'\n", name.c_str());
            return false;
        }
        *out |= found->bit;
    }
    return true;
}

}  // namespace

namespace bh {

// Script format, one entry per line, '#' starts a comment:
//
//     <start> <end> <buttons> [stick_x stick_y] [# note]
//
// Times are seconds since the window opened; buttons are comma-separated names
// or '-' for none; the stick is in the N64's own +/-80 range. Entries overlap
// freely and are OR'd together, which is what makes both a tap (start 4.0, end
// 4.2) and a hold (accelerate for thirty seconds while steering) expressible in
// the same file without two syntaxes.
//
// Buttons prefixed with "2:" ("2:A", "2:-") are player two's. A script that
// uses the prefix at all makes the port report a second controller connected
// (see callbacks.cpp), which is what lets a script reach 2P VS without a second
// pad -- a script with "2:" lines stands in for a second pad.
bool load_input_script() {
    const char* path = std::getenv("BH_INPUT_SCRIPT");
    if (path == nullptr) {
        return false;
    }

    std::ifstream file{ path };
    if (!file) {
        std::fprintf(stderr, "[bh] input script: cannot open %s\n", path);
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const size_t comment = line.find('#');
        std::string note;
        if (comment != std::string::npos) {
            note = line.substr(comment + 1);
            line = line.substr(0, comment);
        }
        std::stringstream fields{ line };
        ScriptEntry entry;
        std::string button_field;
        if (!(fields >> entry.start_seconds >> entry.end_seconds >> button_field)) {
            continue;  // blank or comment-only line
        }
        // "2:A" is player two's A, "2:-" player two holding the stick alone.
        // A script that mentions player two makes that controller present, so
        // two-player modes can be reached without a second pad.
        if (button_field.rfind("2:", 0) == 0) {
            entry.player = 1;
            button_field = button_field.substr(2);
            g_script_has_player_two = true;
        }
        if (!parse_buttons(button_field, &entry.buttons)) {
            std::fprintf(stderr, "[bh] input script: %s:%d\n", path, line_number);
            return false;
        }
        fields >> entry.stick_x >> entry.stick_y;  // optional

        // Trim the note so the log reads cleanly.
        const size_t first = note.find_first_not_of(" \t");
        if (first != std::string::npos) {
            entry.note = note.substr(first);
        }
        g_script.push_back(entry);
    }

    std::sort(g_script.begin(), g_script.end(),
              [](const ScriptEntry& a, const ScriptEntry& b) {
                  return a.start_seconds < b.start_seconds;
              });

    g_script_loaded = !g_script.empty();
    std::fprintf(stderr, "[bh] input script: %zu entries from %s\n", g_script.size(), path);
    std::fflush(stderr);
    return g_script_loaded;
}

bool input_script_has_player_two() {
    return g_script_loaded && g_script_has_player_two;
}

void input_script_state(uint16_t* buttons, float* stick_x, float* stick_y) {
    input_script_state(0, buttons, stick_x, stick_y);
}

void input_script_state(int player, uint16_t* buttons, float* stick_x, float* stick_y) {
    *buttons = 0;
    *stick_x = 0.0f;
    *stick_y = 0.0f;
    if (!g_script_loaded) {
        return;
    }

    const double t = now_seconds();
    for (ScriptEntry& entry : g_script) {
        if (entry.player != player || t < entry.start_seconds || t >= entry.end_seconds) {
            continue;
        }
        if (!entry.announced) {
            entry.announced = true;
            std::fprintf(stderr, "[bh] t=%6.2f input: %s\n", t,
                         entry.note.empty() ? "(no note)" : entry.note.c_str());
            std::fflush(stderr);
        }
        *buttons |= entry.buttons;
        // Last writer wins for the stick; entries are sorted by start time, so
        // a later overlapping entry is the more specific instruction.
        if (entry.stick_x != 0.0f) { *stick_x = entry.stick_x; }
        if (entry.stick_y != 0.0f) { *stick_y = entry.stick_y; }
    }
}

// ------------------------------------------------------- game state watch ---

namespace {

uint8_t* g_rdram = nullptr;

}  // namespace

void set_rdram_base(uint8_t* rdram) {
    g_rdram = rdram;
}

// Body Harvest's game-state variable is not identified yet (phase 04). Until it
// is, the transcript is the file-load log (BH_DEBUG_LOADS, src/sections.cpp).
uint32_t current_game_state() {
    return 0;
}

void poll_game_state() {
    (void)g_rdram;
}

}  // namespace bh
