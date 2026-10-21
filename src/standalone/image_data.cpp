// game.exe's .bss in ei_hook.dll's own storage (S18b; docs/notes/standalone.md, "Own data"). In the
// standalone image the section (kBssBegin-kBssEnd, 0079BF70-007CD000: game_data.h says why it starts
// there) is kept in g_bssStorage instead of at its address: the data seam (game_data.h) adds g_bssShift
// to each of its addresses, the pointer cells of the image's .rdata and .data that point into it
// (image_cells.inc, tools/gen_image_cells.py) get the same shift, and the old range's pages (from
// 0079C000; its page below holds .data's initialised end too) are made PAGE_NOACCESS, so a use that
// missed the seam faults and is named with its data address and code address (OnOldBss). In game.exe
// (the original's runs) none of it happens: the shift stays 0.
//
// The move runs before every other static constructor of ei_hook.dll (init_seg(compiler)): the
// harness and the game's globals bind references to game data at static initialisation.
#include "game/game_data.h"
#include "hook/log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#pragma warning(disable : 4074)   // initialisers put in the compiler's initialisation area: the point
#pragma init_seg(compiler)

namespace ei {

std::uintptr_t g_bssShift = 0;

void DescribeCode(std::uintptr_t address, char* out, std::size_t size);   // hook/standalone.cpp
void ReportImageFailure(const char* what);                                // hook/standalone.cpp

namespace {

constexpr std::uintptr_t kImageBase = 0x00400000;
constexpr std::size_t kBssSize = kBssEnd - kBssBegin;
constexpr std::uintptr_t kKeptBits = 0xFFFF;   // the low 16 bits of each address stay
constexpr std::uintptr_t kPage = 0x1000;
constexpr std::uintptr_t kGuardBegin = (kBssBegin + kPage - 1) & ~(kPage - 1);

// The section's storage: room to start at an address whose low 16 bits are the section's.
alignas(4096) unsigned char g_bssStorage[kBssSize + kKeptBits + 1];

struct Cell {
    std::uintptr_t cell;     // a dword of the image's .rdata or .data
    std::uintptr_t target;   // the .bss address it holds in game.exe
};
constexpr Cell kCells[] = {
#include "image_cells.inc"
};

// An access to the old range: a use of game.exe's .bss that did not go through the seam.
LONG CALLBACK OnOldBss(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t data = r->ExceptionInformation[1];
    if (data - kBssBegin >= kBssSize) return EXCEPTION_CONTINUE_SEARCH;
    char code[160], what[320];
    DescribeCode(reinterpret_cast<std::uintptr_t>(r->ExceptionAddress), code, sizeof(code));
    std::snprintf(what, sizeof(what),
                  "%s of game.exe's .bss at %08X by %s: a use that missed the data seam (GameAddress, game_data.h)",
                  r->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned>(data), code);
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

void MoveBss() {
    if (GetModuleHandleA(nullptr) == reinterpret_cast<HMODULE>(kImageBase)) return;   // game.exe
    const auto storage = reinterpret_cast<std::uintptr_t>(g_bssStorage);
    const std::uintptr_t begin = storage + ((kBssBegin - storage) & kKeptBits);
    // The old range holds zeros (the host's zero fill; .data's file bytes end in zeros there): kept as it is.
    const auto* old = reinterpret_cast<const unsigned char*>(kBssBegin);
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < kBssSize; ++i) nonzero += old[i] != 0;
    std::memcpy(reinterpret_cast<void*>(begin), old, kBssSize);
    g_bssShift = begin - kBssBegin;
    int moved = 0;
    for (const Cell& c : kCells) {
        auto& value = GameData<std::uintptr_t>(c.cell);   // a cell inside the moved range is in its new place
        if (value != c.target) {
            Log("standalone: the cell %08X holds %08X, not .bss's %08X: left", static_cast<unsigned>(c.cell),
                static_cast<unsigned>(value), static_cast<unsigned>(c.target));
            continue;
        }
        value = GameAddress(value);
        ++moved;
    }
    DWORD was;
    const bool guarded =
        VirtualProtect(reinterpret_cast<void*>(kGuardBegin), kBssEnd - kGuardBegin, PAGE_NOACCESS, &was) != 0;
    AddVectoredExceptionHandler(1, OnOldBss);
    Log("standalone: game.exe's .bss kept at %08X (shift %08X), %d of %d cells moved, %u byte(s) already "
        "nonzero, old range %s",
        static_cast<unsigned>(begin), static_cast<unsigned>(g_bssShift), moved, static_cast<int>(std::size(kCells)),
        static_cast<unsigned>(nonzero), guarded ? "PAGE_NOACCESS" : "NOT guarded");
}

struct BssMover {
    BssMover() { MoveBss(); }
} g_bssMover;

} // namespace

} // namespace ei
