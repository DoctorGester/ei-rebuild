// Path cells with plain words in the original code (harness_path_cells.h; ticket SV7d2).
#include "harness_path_cells.h"

#include "../../src/game/ai_map.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace ei::verify::path_cells {

namespace {

constexpr std::uintptr_t kSearchPathTo = 0x005C3200;   // LocalSearch: the path to a cell (RET 0xC)
constexpr std::uintptr_t kBuildCells = 0x005B9A20;     // CAIMap::BuildCellsTo (RET 0xC)
constexpr std::uintptr_t kSmooth = 0x005C46C0;         // PathSmoother::Smooth (RET 0x14)

using SearchPathToFn = void(__fastcall*)(void*, void*, int, int, PathCellList*);
using BuildCellsFn = void(__fastcall*)(CAIMap*, void*, int, int, PathCellList*);
using SmoothFn = int(__fastcall*)(PathSmoother*, void*, CUnit*, CAIMap*, PathCellList*, int, float);

// SearchPathTo clears `path` first, so every cell afterwards is its own.
void __fastcall PlainSearchPathTo(void* search, void*, int x, int y, PathCellList* path) {
    reinterpret_cast<SearchPathToFn>(kSearchPathTo)(search, nullptr, x, y, path);
    SetPlainCellWords(path);
}

// BuildCells clears `path` first too.
void __fastcall PlainBuildCells(CAIMap* map, void*, int x, int y, PathCellList* path) {
    reinterpret_cast<BuildCellsFn>(kBuildCells)(map, nullptr, x, y, path);
    SetPlainCellWords(path);
}

// FindPath's cells have the plain words from the two builders above (or from FindPath itself for
// a single cell); the smoother's rebuilt cells get them here.
int __fastcall PlainSmooth(PathSmoother* smoother, void*, CUnit* unit, CAIMap* map, PathCellList* path,
                           int startDirection, float fraction) {
    int cost = reinterpret_cast<SmoothFn>(kSmooth)(smoother, nullptr, unit, map, path, startDirection, fraction);
    SetPlainCellWords(path);
    return cost;
}

struct Site {
    std::uintptr_t call;     // a CALL rel32
    std::uintptr_t target;   // what it calls
    const void* wrapper;
};
const Site kSites[] = {
    {0x005B97A6, kSearchPathTo, reinterpret_cast<const void*>(&PlainSearchPathTo)},   // JoinLocalSearches 005B9690
    {0x005B97BC, kSearchPathTo, reinterpret_cast<const void*>(&PlainSearchPathTo)},
    {0x005BA337, kBuildCells, reinterpret_cast<const void*>(&PlainBuildCells)},       // PlanLongPath 005B9C90
    {0x005BB8C5, kSmooth, reinterpret_cast<const void*>(&PlainSmooth)},               // FindPath 005BB350
};

bool Redirect(const Site& site) {
    auto* p = reinterpret_cast<unsigned char*>(site.call);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || site.call + 5 + static_cast<std::uintptr_t>(rel) != site.target) {
        Log("harness: %08X is not a call of %08X; its path cells keep the stack's words", unsigned(site.call),
            unsigned(site.target));
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.wrapper) - (site.call + 5));
    ei::NoteHarnessPatch(site.call, 5, "path cells with plain speed words, SV7d2");
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

} // namespace

void Install() {
    if (ei::Standalone()) {
        Log("harness: path cells: the rewrites give the plain words (no original code in the image)");
        return;
    }
    int patched = 0;
    for (const Site& site : kSites)
        if (Redirect(site)) ++patched;
    Log("harness: path cells with plain speed words (%d of %d original sites)", patched,
        static_cast<int>(sizeof(kSites) / sizeof(kSites[0])));
}

} // namespace ei::verify::path_cells
