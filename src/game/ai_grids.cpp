// The AI map's grid template instantiations and the small constructors of the map's members: the local
// search, the unit layers' base, the event table the dirty rectangle's reset; and the terrain types'
// open-cell marks. See ai_map.h and docs/notes/ai.md.
#include "ai_grids.h"

#include "ai_unit_layer.h"
#include "unit_ai.h"
#include "world_data.h"

#include "lib/compiled_vtable.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "game_data.h"

#include <cstring>

extern "C" int _purecall();

// ---- Allocate / free ------------------------------------------------------------------------------

void AllocateNodeGrid(SearchGrid<SearchNode>* grid) { AllocateGrid(*grid); }

void AllocateByteGrid(SearchGrid<std::uint8_t>* grid) { AllocateGrid(*grid); }

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

void AllocateRegionGrid(SearchGrid<std::uint16_t>* grid) { AllocateGrid(*grid); }

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
    for (int i = 0; i < count; ++i) grid->data[i] = (grid->data[i] & 0xFFFFFF60) | 0x100;
}

template <> void SearchGrid<std::uint8_t>::Resize(int newWidth, int newHeight) {
    if (width == newWidth && height == newHeight) return;
    FreeGrid(*this);
    width = newWidth;
    height = newHeight;
    AllocateGrid(*this);
}

template <> void SearchGrid<std::uint8_t>::CopyFrom(const SearchGrid* source) {
    height = source->height;
    width = source->width;
    AllocateGrid(*this);
    int count = width * height;
    if (count > 0) std::memcpy(data, source->data, static_cast<std::size_t>(count));
}

template <> void SearchGrid<SearchNode>::CopyFrom(const SearchGrid* source) {
    height = source->height;
    width = source->width;
    AllocateGrid(*this);
    int count = width * height;
    if (count > 0) std::memcpy(data, source->data, static_cast<std::size_t>(count) * sizeof(SearchNode));
}

// ---- Constructors ---------------------------------------------------------------------------------

template <> SearchGrid<std::int8_t>* SearchGrid<std::int8_t>::Construct(int newWidth, int newHeight) {
    width = newWidth;
    height = newHeight;
    AllocateGrid(*this);
    return this;
}

template <> SearchGrid<SearchNode>* SearchGrid<SearchNode>::Construct(int newWidth, int newHeight) {
    width = newWidth;
    height = newHeight;
    AllocateGrid(*this);
    return this;
}

template <> SearchGrid<AIMapCell>* SearchGrid<AIMapCell>::Construct(int newWidth, int newHeight) {
    height = newHeight;
    width = newWidth;
    AllocateCellGrid(this);
    return this;
}

template <> SearchGrid<std::uint32_t>* SearchGrid<std::uint32_t>::Construct(int newWidth, int newHeight) {
    height = newHeight;
    width = newWidth;
    AllocateOccluderGrid(this);
    return this;
}

void ConstructEdgeGrid(SearchGrid<std::int16_t>* grid) {
    grid->width = 1;
    grid->height = 1;
    AllocateEdgeGrid(grid);
}

void ConstructRepresentativeGrid(SearchGrid<std::uint8_t>* grid) {
    grid->width = 1;
    grid->height = 1;
    AllocateZeroedByteGrid(grid);
}

void ConstructRegionGrid(SearchGrid<std::uint16_t>* grid) {
    grid->width = 1;
    grid->height = 1;
    AllocateGrid(*grid);
}

LocalSearch* LocalSearch::Construct(CAIMap* owner) {
    nodes.width = 1;
    nodes.height = 1;
    AllocateGrid(nodes);
    levels.width = 1;
    levels.height = 1;
    AllocateGrid(levels);
    cellRowsVtable = mfc::impl::CellRowArrayClass::Vtable();
    cellRows = nullptr;
    cellRowGrowBy = 0;
    cellRowMax = 0;
    cellRowCount = 0;
    map = nullptr;
    if (owner) Attach(owner, false);
    return this;
}

void LocalSearch::Destruct() {
    cellRowsVtable = mfc::impl::CellRowArrayClass::Vtable();
    if (cellRows) gamecrt::operator_delete(cellRows);
    FreeGrid(levels);
    FreeGrid(nodes);
}

CUnitLayer::CUnitLayer() {
    for (UnitLayerNode*& bucket : buckets) bucket = nullptr;
    freeNodes = nullptr;
}

CUnitLayer::~CUnitLayer() = default;

void AIDirtyRect::Reset() {
    minX = 10000;
    maxX = 0;
    minY = 10000;
    maxY = 0;
}

// ---- The event table ------------------------------------------------------------------------------

AIEvent* g_freeEvents = /*007C53F0*/ {nullptr};

namespace {
AIEvent*& FreeEvents() { return g_freeEvents; }

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

// ---- Terrain types --------------------------------------------------------------------------------

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
