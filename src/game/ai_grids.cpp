// The AI map's grid allocators and the small constructors of the map's members: the local and block searches, the
// event table, the dirty rectangle's reset; and the terrain types' open-cell marks. See ai_map.h and
// docs/notes/ai.md.
#include "ai_grids.h"

#include "ai_unit_layer.h"
#include "unit_ai.h"
#include "world_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


extern "C" int _purecall();

// ---- Allocate / free ------------------------------------------------------------------------------

void AllocateEdgeGrid(SearchGrid<std::int16_t>* grid) {
    constexpr int kEdges = 8;
    AllocateGrid(*grid, kEdges);
    int count = grid->width * grid->height * kEdges;
    for (int i = 0; i < count; ++i) grid->data[i] = -1;
}

void AllocateZeroedByteGrid(SearchGrid<std::uint8_t>* grid) {
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    for (int i = 0; i < count; ++i) grid->data[i] = 0;
}

void AllocateCellGrid(SearchGrid<AIMapCell>* grid) {
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    for (int i = 0; i < count; ++i) {
        AIMapCell& cell = grid->data[i];
        cell.flags = 0;
        cell.passable = 0;
        cell.groundLevel = 0;
        cell.level = 0;
    }
}

void AllocateOccluderGrid(SearchGrid<std::uint32_t>* grid) {
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    for (int i = 0; i < count; ++i) grid->data[i] = (grid->data[i] & ~(occluder::kTypeMask | occluder::kChained)) | occluder::kNone;
}

// ---- Constructors ---------------------------------------------------------------------------------

LocalSearch::LocalSearch() {
    ResizeGrid(nodes, 1, 1);
    ResizeGrid(levels, 1, 1);
}

BlockSearch::BlockSearch() { ResizeGrid(records, 1, 1); }

void AIDirtyRect::Reset() {
    minX = 10000;
    maxX = 0;
    minY = 10000;
    maxY = 0;
}

// ---- The event table ------------------------------------------------------------------------------

AIEvent* g_freeEvents = /*007C53F0*/ {nullptr};

namespace {

void AllocateEventCells(AIEventTable* table) {
    table->cells = new AIEventCell[static_cast<std::size_t>(table->width * table->height)];
    table->rows = new AIEventCell*[static_cast<std::size_t>(table->height)];
    for (int i = 0; i < table->height; ++i) table->rows[i] = table->cells + i * table->width;
}

// The mark table's rows are 16 wide; column 10 is the open-cell mark of a cost band.
int OpenMark(int band) { return static_cast<int>(worlddata::kMarkTable[band * 16 + 10]); }

// The cost bands: 0 or 1, 2, 3..9, 10..34, 35..70 (above: no mark).
int CostBand(int cost) {
    if (cost >= 10) return cost >= 35 ? 4 : 3;
    if (cost >= 3) return 2;
    return cost >= 2 ? 1 : 0;
}

} // namespace

AIEventTable::AIEventTable() {
    AllocateEventCells(this);
    Clear();
}

AIEventTable::~AIEventTable() {
    for (auto& slot : slots) {
        for (AIEvent* head : slot) {
            for (AIEvent* event = head; event; event = event->nextInSlot) {
                event->next = g_freeEvents;
                g_freeEvents = event;
            }
        }
    }
    delete[] cells;
    delete[] rows;
}

void AIEventTable::Clear() {
    tick = -1;
    std::memset(slots, 0, sizeof(slots));
    if (width != 1 || height != 1) {
        delete[] cells;
        delete[] rows;
        width = 1;
        height = 1;
        AllocateEventCells(this);
    }
    std::memset(rows[0], 0, sizeof(AIEventCell));
}

// ---- Terrain types --------------------------------------------------------------------------------

void CAIMap::BuildOpenMarks() {
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
