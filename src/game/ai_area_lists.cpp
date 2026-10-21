// 005B4150 (thiscall on CAIMap, from the constructor): fills the six per-level area lists (+0x204
// .. +0x268, CArray<{int, int}> of 0x14 bytes) from their tables in the game's data, each a run
// of {a, b} pairs ended by a = 100 (007912DC, 0079130C, 0079137C, 0079146C, 007914BC,
// 0079156C). The last list appends through its Add (005BDD70), the others through SetSize
// (005BEC40) as the original inlines it.
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"
#include "game_data.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

using PairArray = mfc::CArray<AreaOffset>;

constexpr std::uintptr_t kLevel0Table = ei::DataId(0x007912DC);
constexpr std::uintptr_t kLevel1Table = ei::DataId(0x0079130C);
constexpr std::uintptr_t kLevel2Table = ei::DataId(0x0079137C);
constexpr std::uintptr_t kLevel3Table = ei::DataId(0x0079146C);
constexpr std::uintptr_t kLevel4Table = ei::DataId(0x007914BC);
constexpr std::uintptr_t kLevel5Table = ei::DataId(0x0079156C);
constexpr std::uintptr_t kTables[6] = {kLevel0Table, kLevel1Table, kLevel2Table, kLevel3Table, kLevel4Table, kLevel5Table};
constexpr int kEnd = 100;

} // namespace

void __fastcall FillAreaLists(CAIMap* map) {
    for (int i = 0; i < 6; ++i) {
        PairArray* list = &map->areaLists[i];
        list->SetSize(0, -1);
        for (const int* entry = ei::GamePointer<const int>(kTables[i]); entry[0] != kEnd; entry += 2) {
            AreaOffset pair = {entry[0], entry[1]};
            if (i == 5) {
                list->Add(pair);
                continue;
            }
            int n = list->size;
            list->SetSize(n + 1, -1);
            list->data[n].dx = pair.dx;
            list->data[n].dy = pair.dy;
        }
    }
}

EI_REPLACE(0x005B4150, FillAreaLists);
