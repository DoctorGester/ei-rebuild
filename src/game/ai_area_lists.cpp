// Fills the six per-level area lists of CAIMap (CArray<{int, int}>) from their tables in the game's data, each
// a run of {a, b} pairs ended by a = 100. The last list appends through its Add, the others through SetSize.
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"
#include "game_data.h"

#include <cstdint>
#include <vector>

namespace {

constexpr int kLevel0Table[12] = /*007912DC*/ {
    0, 0, 1, 0, -1, 0, 0, 1, 0, -1, 100, 100};
constexpr int kLevel1Table[28] = /*0079130C*/ {
    -2, 0, -1, -1, -1, 0, -1, 1, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 1, -1, 1, 0, 1, 1, 2, 0, 100, 100};
constexpr int kLevel2Table[60] = /*0079137C*/ {
    -3, 0, -2, -2, -2, -1, -2, 0, -2, 1, -2, 2, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, 0, -3, 0, -2, 0, -1, 0, 0, 0,
    1, 0, 2, 0, 3, 1, -2, 1, -1, 1, 0, 1, 1, 1, 2, 2, -2, 2, -1, 2, 0, 2, 1, 2, 2, 3, 0, 100, 100};
constexpr int kLevel3Table[20] = /*0079146C*/ {
    -1, -1, -1, 0, -1, 1, 0, -1, 0, 0, 0, 1, 1, -1, 1, 0, 1, 1, 100, 100};
constexpr int kLevel4Table[44] = /*007914BC*/ {
    -2, -1, -2, 0, -2, 1, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 1, -2, 1, -1, 1, 0,
    1, 1, 1, 2, 2, -1, 2, 0, 2, 1, 100, 100};
constexpr int kLevel5Table[76] = /*0079156C*/ {
    -3, -1, -3, 0, -3, 1, -2, -2, -2, -1, -2, 0, -2, 1, -2, 2, -1, -3, -1, -2, -1, -1, -1, 0, -1, 1, -1, 2, -1, 3,
    0, -3, 0, -2, 0, -1, 0, 0, 0, 1, 0, 2, 0, 3, 1, -3, 1, -2, 1, -1, 1, 0, 1, 1, 1, 2, 1, 3, 2, -2, 2, -1, 2, 0,
    2, 1, 2, 2, 3, -1, 3, 0, 3, 1, 100, 100};
constexpr const int* kTables[6] = {kLevel0Table, kLevel1Table, kLevel2Table, kLevel3Table, kLevel4Table, kLevel5Table};
constexpr int kEnd = 100;

} // namespace

void FillAreaLists(CAIMap* map) {
    for (int i = 0; i < 6; ++i) {
        std::vector<AreaOffset>* list = &map->areaLists[i];
        list->clear();
        for (const int* entry = kTables[i]; entry[0] != kEnd; entry += 2) {
            AreaOffset pair = {entry[0], entry[1]};
            if (i == 5) {
                list->push_back(pair);
                continue;
            }
            int n = std::ssize(*list);
            list->resize(n + 1);
            (*list)[n].dx = pair.dx;
            (*list)[n].dy = pair.dy;
        }
    }
}
