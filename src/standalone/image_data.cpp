// game.exe's .rdata and .data in ei_hook.dll's own storage (S18b, S18d, S18h; docs/notes/standalone.md, "Own data").
// In the standalone image both sections (kMovedBegin-kMovedEnd, 0073B000-007CD000: .rdata with the import address
// table at its start, .data with its zero-filled tail, the .bss) are kept in g_dataStorage instead of at their
// addresses: the data seam (game_data.h) adds g_dataShift to each of their addresses, the pointer cells of
// game.exe's .rdata and .data that point into the range (image_cells.inc, tools/gen_image_cells.py: vtables'
// RTTI, the C++ exception tables, MFC's runtime classes and maps, string pointers) get the same shift. Nothing is at
// the old range (the host keeps what of game.exe's range is free reserved, S18k), so a use that missed the seam
// faults and is named with its data address and code address (OnOldData); a game address outside the range asked
// of the seam is named too (StrayGameAddress). The initial bytes come from game.exe at build time
// (image_data_bytes.inc in the build folder, gen_image_cells.py --bytes).
// In game.exe (the original's runs) none of it happens: the shift stays 0.
//
// The import slots at the storage's start are ours: zero from the build, resolved right after the move
// (image_imports.cpp, S18f).
//
// The move runs before every other static constructor of ei_hook.dll (init_seg(compiler)): the
// harness and the game's globals bind references to game data at static initialisation, and the harness
// takes import slots there.
#include "ei/hook.h"
#include "game/game_data.h"
#include "hook/log.h"

#include <windows.h>
#include <intrin.h>

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
// "<dll>!<function>" of game.exe's import slot `slot` into `out`.
void DescribeImportSlot(std::uintptr_t slot, char* out, std::size_t size);   // image_imports.cpp

namespace {

constexpr std::size_t kMovedSize = kMovedEnd - kMovedBegin;
constexpr std::uintptr_t kDataBegin = DataId(0x00783000);   // .data, after .rdata's pages
constexpr std::uintptr_t kKeptBits = 0xFFFF;   // the low 16 bits of each address stay
constexpr std::uintptr_t kPage = 0x1000;
static_assert(kMovedBegin % kPage == 0 && kMovedEnd % kPage == 0, "the guard is the sections' whole pages");

// The storage: room to start at an address whose low 16 bits are .rdata's.
alignas(4096) unsigned char g_dataStorage[kMovedSize + kKeptBits + 1];

// The sections' bytes as game.exe's file holds them (the import slots zero), up to the last nonzero dword (the
// rest is zero).
constexpr std::uint32_t kInitialData[] = {
#include "image_data_bytes.inc"
};
static_assert(sizeof(kInitialData) <= kMovedSize);

struct Cell {
    std::uintptr_t cell;     // a dword of the image's .rdata or .data
    std::uintptr_t target;   // the .rdata or .data address it holds in game.exe
};
constexpr Cell kCells[] = {
#include "image_cells.inc"
};

// An access to the old range: a use of game.exe's .rdata or .data that did not go through the seam (a call through
// game.exe's own import slot among them).
LONG CALLBACK OnOldData(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t data = r->ExceptionInformation[1];
    if (data - kMovedBegin >= kMovedSize) return EXCEPTION_CONTINUE_SEARCH;
    char code[160], import[160], what[480];
    DescribeCode(reinterpret_cast<std::uintptr_t>(r->ExceptionAddress), code, sizeof(code));
    const char* access = r->ExceptionInformation[0] == 1 ? "write" : "read";
    if (data - kImportsBegin < kImportsEnd - kImportsBegin) {
        DescribeImportSlot(data & ~std::uintptr_t{3}, import, sizeof(import));
        std::snprintf(what, sizeof(what),
                      "%s of game.exe's own import slot %08X (%s) by %s: the image's slots are ei_hook.dll's table, a "
                      "use that missed the data seam (GameAddress, game_data.h)",
                      access, static_cast<unsigned>(data), import, code);
    } else {
        std::snprintf(what, sizeof(what),
                      "%s of game.exe's %s at %08X by %s: a use that missed the data seam (GameAddress, game_data.h)",
                      access, data < kDataBegin ? ".rdata" : ".data", static_cast<unsigned>(data), code);
    }
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

void MoveData() {
    if (!Standalone()) return;   // game.exe
    const auto storage = reinterpret_cast<std::uintptr_t>(g_dataStorage);
    const std::uintptr_t begin = storage + ((kMovedBegin - storage) & kKeptBits);
    std::memcpy(reinterpret_cast<void*>(begin), kInitialData, sizeof(kInitialData));
    g_dataShift = begin - kMovedBegin;
    int moved = 0;
    for (const Cell& c : kCells) {
        auto& value = GameData<std::uintptr_t>(c.cell);   // a cell inside the moved range is in its new place
        if (value != c.target) {
            Log("standalone: the cell %08X holds %08X, not game.exe's %08X: left", static_cast<unsigned>(c.cell),
                static_cast<unsigned>(value), static_cast<unsigned>(c.target));
            continue;
        }
        value = GameAddress(value);
        ++moved;
    }
    AddVectoredExceptionHandler(1, OnOldData);
    Log("standalone: game.exe's .rdata and .data kept at %08X (shift %08X), %u bytes from the build, %d of %d cells moved",
        static_cast<unsigned>(begin), static_cast<unsigned>(g_dataShift), static_cast<unsigned>(sizeof(kInitialData)),
        moved, static_cast<int>(std::size(kCells)));
    ResolveImageImports();
}

struct DataMover {
    DataMover() { MoveData(); }
} g_dataMover;

} // namespace

void StrayGameAddress(std::uintptr_t address) {
    char code[160], what[320];
    DescribeCode(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), code, sizeof(code));
    const char* part = address < 0x00401000 ? "headers" : address < kMovedBegin ? "code" : ".rsrc";
    std::snprintf(what, sizeof(what),
                  "the data seam was asked for game.exe's %s at %08X (by %s): the image has nothing of game.exe there",
                  part, static_cast<unsigned>(address), code);
    std::fprintf(stderr, "ei_hook: %s\n", what);
    std::fflush(stderr);
    ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

} // namespace ei
