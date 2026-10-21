// game.exe's .data in ei_hook.dll's own storage (S18b, S18d; docs/notes/standalone.md, "Own data"). In the
// standalone image the whole section (kMovedBegin-kMovedEnd, 00783000-007CD000, its zero-filled tail, the
// .bss, included) is kept in g_dataStorage instead of at its address: the data seam (game_data.h) adds
// g_dataShift to each of its addresses, the pointer cells of game.exe's .rdata and .data that point into it
// (image_cells.inc, tools/gen_image_cells.py) get the same shift, and the old range's pages are made
// PAGE_NOACCESS, so a use that missed the seam faults and is named with its data address and code address
// (OnOldData). The section's initial bytes come from game.exe at build time (image_data_bytes.inc in the build
// folder, gen_image_cells.py --bytes); the host leaves the old range zero. In game.exe (the original's runs)
// none of it happens: the shift stays 0.
//
// The storage starts at game.exe's import address table (kImportsBegin, 0073B000): its slots are ours at the
// same shift (image_imports.cpp, S18f), resolved right after the move. The .rdata between the table and .data
// is still read at its address (until S18h moves it into the gap).
//
// The move runs before every other static constructor of ei_hook.dll (init_seg(compiler)): the
// harness and the game's globals bind references to game data at static initialisation, and the harness
// takes import slots there.
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

std::uintptr_t g_dataShift = 0;

void DescribeCode(std::uintptr_t address, char* out, std::size_t size);   // hook/standalone.cpp
void ReportImageFailure(const char* what);                                // hook/standalone.cpp
void ResolveImageImports();                                              // image_imports.cpp

namespace {

constexpr std::uintptr_t kImageBase = 0x00400000;
constexpr std::size_t kMovedSize = kMovedEnd - kMovedBegin;
constexpr std::uintptr_t kStorageBegin = kImportsBegin;   // the import slots, then .rdata's place, then .data
constexpr std::size_t kStorageSize = kMovedEnd - kStorageBegin;
constexpr std::uintptr_t kKeptBits = 0xFFFF;   // the low 16 bits of each address stay
constexpr std::uintptr_t kPage = 0x1000;
static_assert(kMovedBegin % kPage == 0 && kMovedEnd % kPage == 0, "the guard is the section's whole pages");

// The storage: room to start at an address whose low 16 bits are the import table's.
alignas(4096) unsigned char g_dataStorage[kStorageSize + kKeptBits + 1];

// The section's bytes as game.exe's file holds them, up to the last nonzero dword (the rest is zero).
constexpr std::uint32_t kInitialData[] = {
#include "image_data_bytes.inc"
};
static_assert(sizeof(kInitialData) <= kMovedSize);

struct Cell {
    std::uintptr_t cell;     // a dword of the image's .rdata or .data
    std::uintptr_t target;   // the .data address it holds in game.exe
};
constexpr Cell kCells[] = {
#include "image_cells.inc"
};

// An access to the old range: a use of game.exe's .data that did not go through the seam.
LONG CALLBACK OnOldData(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t data = r->ExceptionInformation[1];
    if (data - kMovedBegin >= kMovedSize) return EXCEPTION_CONTINUE_SEARCH;
    char code[160], what[320];
    DescribeCode(reinterpret_cast<std::uintptr_t>(r->ExceptionAddress), code, sizeof(code));
    std::snprintf(what, sizeof(what),
                  "%s of game.exe's .data at %08X by %s: a use that missed the data seam (GameAddress, game_data.h)",
                  r->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned>(data), code);
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

void MoveData() {
    if (GetModuleHandleA(nullptr) == reinterpret_cast<HMODULE>(kImageBase)) return;   // game.exe
    const auto storage = reinterpret_cast<std::uintptr_t>(g_dataStorage);
    const std::uintptr_t base = storage + ((kStorageBegin - storage) & kKeptBits);
    const std::uintptr_t begin = base + (kMovedBegin - kStorageBegin);
    // The host lays out no .data: the old range should be all zero (a byte that is not means it still copies).
    const auto* old = reinterpret_cast<const unsigned char*>(kMovedBegin);
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < kMovedSize; ++i) nonzero += old[i] != 0;
    std::memcpy(reinterpret_cast<void*>(begin), kInitialData, sizeof(kInitialData));
    g_dataShift = base - kStorageBegin;
    int moved = 0;
    for (const Cell& c : kCells) {
        auto& value = GameData<std::uintptr_t>(c.cell);   // a cell inside the moved range is in its new place
        if (value != c.target) {
            Log("standalone: the cell %08X holds %08X, not .data's %08X: left", static_cast<unsigned>(c.cell),
                static_cast<unsigned>(value), static_cast<unsigned>(c.target));
            continue;
        }
        value = GameAddress(value);
        ++moved;
    }
    DWORD was;
    const bool guarded =
        VirtualProtect(reinterpret_cast<void*>(kMovedBegin), kMovedSize, PAGE_NOACCESS, &was) != 0;
    AddVectoredExceptionHandler(1, OnOldData);
    Log("standalone: game.exe's .data kept at %08X (shift %08X), %u bytes from the build, %d of %d cells moved, "
        "%u byte(s) of the old range nonzero, old range %s",
        static_cast<unsigned>(begin), static_cast<unsigned>(g_dataShift), static_cast<unsigned>(sizeof(kInitialData)),
        moved, static_cast<int>(std::size(kCells)), static_cast<unsigned>(nonzero),
        guarded ? "PAGE_NOACCESS" : "NOT guarded");
    ResolveImageImports();
}

struct DataMover {
    DataMover() { MoveData(); }
} g_dataMover;

} // namespace

} // namespace ei
