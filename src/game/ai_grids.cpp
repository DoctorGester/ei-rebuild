// The AI map's grid template instantiations (ai_grids.h) and the small constructors of the map's
// members: the local search (005B3FF0), the unit layers' base (005B4110), the event table
// (005B3F40 / 005C9250 / 005C91F0), the dirty rectangle's reset (005B40F0); and the terrain types'
// open-cell marks (005B8620). See ai_map.h and docs/notes/ai.md.
#include "ai_grids.h"

#include "ai_unit_layer.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstring>

// ---- Allocate / free ------------------------------------------------------------------------------

void __fastcall AllocateNodeGrid(SearchGrid<SearchNode>* grid) { AllocateGrid(*grid); }
EI_REPLACE(0x005B1470, AllocateNodeGrid);

void __fastcall AllocateByteGrid(SearchGrid<std::uint8_t>* grid) { AllocateGrid(*grid); }
EI_REPLACE(0x005B14C0, AllocateByteGrid);

void __fastcall AllocateBlockRecordGrid(SearchGrid<BlockRecord>* grid) { AllocateGrid(*grid); }
EI_REPLACE(0x005B1500, AllocateBlockRecordGrid);

void __fastcall AllocateEdgeGrid(SearchGrid<std::int16_t>* grid) {
    constexpr int kEdges = 8;
    AllocateGrid(*grid, kEdges);
    int count = grid->width * grid->height * kEdges;
    for (int i = 0; i < count; ++i) grid->data[i] = -1;
}
EI_REPLACE(0x005B1550, AllocateEdgeGrid);

void __fastcall AllocateZeroedByteGrid(SearchGrid<std::uint8_t>* grid) {
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    for (int i = 0; i < count; ++i) grid->data[i] = 0;
}
EI_REPLACE(0x005B15F0, AllocateZeroedByteGrid);

void __fastcall AllocateRegionGrid(SearchGrid<std::uint16_t>* grid) { AllocateGrid(*grid); }
EI_REPLACE(0x005B1680, AllocateRegionGrid);

void __fastcall AllocateCellGrid(SearchGrid<AIMapCell>* grid) {
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
EI_REPLACE(0x005BD030, AllocateCellGrid);

void __fastcall AllocateOccluderGrid(SearchGrid<std::uint32_t>* grid) {
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    for (int i = 0; i < count; ++i) grid->data[i] = (grid->data[i] & 0xFFFFFF60) | 0x100;
}
EI_REPLACE(0x005BD0B0, AllocateOccluderGrid);

void __fastcall FreeAnyGrid(SearchGrid<std::uint8_t>* grid) { FreeGrid(*grid); }
EI_REPLACE(0x005BD010, FreeAnyGrid);
// Byte-identical copies (the other element types' destructors).
EI_REPLACE(0x00598400, FreeAnyGrid);
EI_REPLACE(0x00598420, FreeAnyGrid);
EI_REPLACE(0x00598440, FreeAnyGrid);
EI_REPLACE(0x00598460, FreeAnyGrid);
EI_REPLACE(0x00598480, FreeAnyGrid);
EI_REPLACE(0x00598580, FreeAnyGrid);
EI_REPLACE(0x005985A0, FreeAnyGrid);
EI_REPLACE(0x00598A60, FreeAnyGrid);
EI_REPLACE(0x00598A80, FreeAnyGrid);
EI_REPLACE(0x00598AA0, FreeAnyGrid);
EI_REPLACE(0x00598B00, FreeAnyGrid);
EI_REPLACE(0x005B1450, FreeAnyGrid);
EI_REPLACE(0x005BCC30, FreeAnyGrid);
EI_REPLACE(0x005CD490, FreeAnyGrid);
EI_REPLACE(0x0058AAB0, FreeAnyGrid);   // A-W14: only the world server's unwind funclets jump to it

void __fastcall AllocateEventGrid(SearchGrid<AIEventCell>* grid) { AllocateGrid(*grid); }
EI_REPLACE(0x005CD4B0, AllocateEventGrid);

void __fastcall ResizeByteGrid(SearchGrid<std::uint8_t>* grid, void*, int width, int height) {
    if (grid->width == width && grid->height == height) return;
    FreeGrid(*grid);
    grid->width = width;
    grid->height = height;
    AllocateGrid(*grid);
}
EI_REPLACE(0x005CD3A0, ResizeByteGrid);

void __fastcall CopyByteGrid(SearchGrid<std::uint8_t>* grid, void*, const SearchGrid<std::uint8_t>* source) {
    grid->height = source->height;
    grid->width = source->width;
    AllocateGrid(*grid);
    int count = grid->width * grid->height;
    if (count > 0) std::memcpy(grid->data, source->data, static_cast<std::size_t>(count));
}
EI_REPLACE(0x005CD410, CopyByteGrid);

// ---- Constructors ---------------------------------------------------------------------------------

SearchGrid<std::int8_t>* __fastcall ConstructByteGrid(SearchGrid<std::int8_t>* grid, void*, int width, int height) {
    grid->width = width;
    grid->height = height;
    AllocateGrid(*grid);
    return grid;
}
EI_REPLACE(0x005BCBD0, ConstructByteGrid);

SearchGrid<SearchNode>* __fastcall ConstructNodeGrid(SearchGrid<SearchNode>* grid, void*, int width, int height) {
    grid->width = width;
    grid->height = height;
    AllocateGrid(*grid);
    return grid;
}
EI_REPLACE(0x005BCC50, ConstructNodeGrid);

SearchGrid<AIMapCell>* __fastcall ConstructCellGrid(SearchGrid<AIMapCell>* grid, void*, int width, int height) {
    grid->height = height;
    grid->width = width;
    AllocateCellGrid(grid);
    return grid;
}
EI_REPLACE(0x005BCEE0, ConstructCellGrid);

SearchGrid<std::uint32_t>* __fastcall ConstructOccluderGrid(SearchGrid<std::uint32_t>* grid, void*, int width,
                                                           int height) {
    grid->height = height;
    grid->width = width;
    AllocateOccluderGrid(grid);
    return grid;
}
EI_REPLACE(0x005BCF00, ConstructOccluderGrid);

void __fastcall ConstructRegionGrid(SearchGrid<std::uint16_t>* grid) {
    grid->width = 1;
    grid->height = 1;
    AllocateGrid(*grid);
}
EI_REPLACE(0x005AF5A0, ConstructRegionGrid);

LocalSearch* LocalSearch::Construct(CAIMap* owner) {
    nodes.width = 1;
    nodes.height = 1;
    AllocateGrid(nodes);
    levels.width = 1;
    levels.height = 1;
    AllocateGrid(levels);
    cellRowsVtable = reinterpret_cast<void*>(kCellRowArrayVtable);
    cellRows = nullptr;
    cellRowGrowBy = 0;
    cellRowMax = 0;
    cellRowCount = 0;
    map = nullptr;
    if (owner) Attach(owner, false);
    return this;
}
EI_REPLACE_METHOD(0x005B3FF0, LocalSearch::Construct);

CUnitLayer* CUnitLayer::Construct() {
    listVtable = reinterpret_cast<void*>(kNodeListVtable);
    count = 0;
    free = nullptr;
    tail = nullptr;
    head = nullptr;
    blocks = nullptr;
    blockSize = 0x400;
    vtable = reinterpret_cast<void*>(std::uintptr_t(0x0073FD7C));
    for (UnitLayerNode*& bucket : buckets) bucket = nullptr;
    freeNodes = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x005B4110, CUnitLayer::Construct);

void CUnitLayer::Destruct() {
    listVtable = reinterpret_cast<void*>(kNodeListVtable);
    count = 0;
    free = nullptr;
    tail = nullptr;
    head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(blocks));
    blocks = nullptr;
}
EI_REPLACE_METHOD(0x0058A8A0, CUnitLayer::Destruct);
EI_REPLACE_METHOD(0x0058A900, CUnitLayer::Destruct);   // the other layer class's byte-identical copy

void AIDirtyRect::Reset() {
    minX = 10000;
    maxX = 0;
    minY = 10000;
    maxY = 0;
}
EI_REPLACE_METHOD(0x005B40F0, AIDirtyRect::Reset);

AITerrainType* __fastcall TerrainTypeAt(AITerrainType** types, void*, int index) { return *types + index; }
EI_REPLACE(0x005BDD50, TerrainTypeAt);

// ---- The event table ------------------------------------------------------------------------------

namespace {
AIEvent*& FreeEvents() { return *reinterpret_cast<AIEvent**>(std::uintptr_t(0x007C53F0)); }

void AllocateEventCells(AIEventTable* table) {
    table->cells = static_cast<AIEventCell*>(
        gamecrt::operator_new(static_cast<std::size_t>(table->width * table->height) * sizeof(AIEventCell)));
    table->rows =
        static_cast<AIEventCell**>(gamecrt::operator_new(static_cast<std::size_t>(table->height) * sizeof(AIEventCell*)));
    for (int i = 0; i < table->height; ++i) table->rows[i] = table->cells + i * table->width;
}
} // namespace

AIEventTable* AIEventTable::Construct() {
    width = 1;
    height = 1;
    AllocateEventCells(this);
    Clear();
    return this;
}
EI_REPLACE_METHOD(0x005B3F40, AIEventTable::Construct);

void AIEventTable::Clear() {
    tick = -1;
    std::memset(slots, 0, sizeof(slots));
    if (width != 1 || height != 1) {
        gamecrt::operator_delete(cells);
        gamecrt::operator_delete(rows);
        width = 1;
        height = 1;
        AllocateEventCells(this);
    }
    std::memset(rows[0], 0, sizeof(AIEventCell));
}
EI_REPLACE_METHOD(0x005C9250, AIEventTable::Clear);

void AIEventTable::Destroy() {
    for (auto& slot : slots) {
        for (AIEvent* head : slot) {
            for (AIEvent* event = head; event; event = event->nextInSlot) {
                event->next = FreeEvents();
                FreeEvents() = event;
            }
        }
    }
    gamecrt::operator_delete(cells);
    gamecrt::operator_delete(rows);
}
EI_REPLACE_METHOD(0x005C91F0, AIEventTable::Destroy);

// ---- Terrain types --------------------------------------------------------------------------------

namespace {
// 0079118C: rows of 16 ints; the first of each row is the open-cell mark of a cost band.
int OpenMark(int band) { return reinterpret_cast<const int*>(std::uintptr_t(0x0079118C))[band * 16]; }

// The cost bands: 0 or 1, 2, 3..9, 10..34, 35..70 (above: no mark).
int CostBand(int cost) {
    if (cost >= 10) return cost >= 35 ? 4 : 3;
    if (cost >= 3) return 2;
    return cost >= 2 ? 1 : 0;
}
} // namespace

void CAIMap::BuildOpenMarks() {
    for (int i = 0; i < materialCount; ++i) {
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
EI_REPLACE_METHOD(0x005B8620, CAIMap::BuildOpenMarks);
