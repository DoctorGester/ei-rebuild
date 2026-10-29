// The small constructors of the AI map's members: the local and block searches, the event table, the dirty
// rectangle's reset; and the terrain types' open-cell marks. See ai_map.h and
// docs/notes/ai.md.
#include "ai_map.h"

#include "ai_unit_layer.h"
#include "unit_ai.h"
#include "world_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


extern "C" int _purecall();

// ---- Constructors ---------------------------------------------------------------------------------

LocalSearch::LocalSearch() {
    nodes.Resize(1, 1);
    levels.Resize(1, 1);
}

BlockSearch::BlockSearch() { records.Resize(1, 1); }

void AIDirtyRect::Reset() {
    minX = 10000;
    maxX = 0;
    minY = 10000;
    maxY = 0;
}

// ---- The event table ------------------------------------------------------------------------------

AIEvent* g_freeEvents = /*007C53F0*/ {nullptr};

namespace {

// The mark table's rows are 16 wide; column 10 is the open-cell mark of a cost band.
int OpenMark(int band) { return static_cast<int>(worlddata::kMarkTable[band * 16 + 10]); }

// The cost bands: 0 or 1, 2, 3..9, 10..34, 35..70 (above: no mark).
int CostBand(int cost) {
    if (cost >= 10) return cost >= 35 ? 4 : 3;
    if (cost >= 3) return 2;
    return cost >= 2 ? 1 : 0;
}

} // namespace

AIEventTable::AIEventTable() { Clear(); }

AIEventTable::~AIEventTable() {
    for (auto& slot : slots) {
        for (AIEvent* head : slot) {
            for (AIEvent* event = head; event; event = event->nextInSlot) {
                event->next = g_freeEvents;
                g_freeEvents = event;
            }
        }
    }
}

void AIEventTable::Clear() {
    tick = -1;
    std::memset(slots, 0, sizeof(slots));
    width = 1;
    height = 1;
    cells.assign(1, AIEventCell{});
}

// ---- Terrain types --------------------------------------------------------------------------------

void AIMap::BuildOpenMarks() {
    for (int i = 0; i < std::ssize(materials); ++i) {
        std::uint32_t packed = 0;
        for (int sizeClass = 0; sizeClass < 8; ++sizeClass) {
            // The smallest size class is marked as if every type cost 1. (The original also
            // scales a per-class height here and drops the result.)
            int cost = sizeClass == 0 ? 1 : materials[i].cost;
            int mark = cost > 70 ? 0 : OpenMark(CostBand(cost));
            packed += static_cast<std::uint32_t>(mark) << (sizeClass * 4);
        }
        materials[i].openMarks = packed;
    }
}
