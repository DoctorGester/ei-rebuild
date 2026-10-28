// The CAIMap constructor, destructor and reset (destroyed, then constructed again in place). The unit layers
// and the sector map are C++ members; the inlined constructors of the others (CArrays, CLists, the dirty
// rectangle) are stores. The original's exception frames only matter when a member constructor throws, which
// ends the game either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"

#include <cstdint>
#include "unit_ai.h"
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "record_arrays.h"

CAIMap::CAIMap() {
    climbLarge.Construct(mfc::impl::IntArrayClass::Vtable());
    climbSmall.Construct(mfc::impl::IntArrayClass::Vtable());
    bridgeHeights.Construct(100);
    footprint.hashed = true;
    bridges.hashed = true;
    waterDepth.width = 1;
    waterDepth.height = 1;
    AllocateGrid(waterDepth);
    for (mfc::CArray<AreaOffset>& list : areaLists) list.Construct(mfc::impl::AreaListClass::Vtable());
    unitLayerSmall.map = this;
    unitLayerLarge.map = this;
    ConstructRegionMap(&regionMap);
    forwardSearch.nodes.Construct(1, 1);
    forwardSearch.levels.width = 1;
    forwardSearch.levels.height = 1;
    AllocateGrid(forwardSearch.levels);
    mfc::impl::ConstructHeader<mfc::impl::CellRowArrayClass>(&forwardSearch.cellRowsVtable);
    forwardSearch.map = nullptr;
    backwardSearch.Construct(nullptr);
    cells.Construct(1, 1);
    occluders.Construct(1, 1);
    marks.Construct(1, 1);
    Events()->Construct();
    loaded = false;
    batching = false;
    dirty.Reset();
    FillAreaLists(this);
    footprintTick = 0;
    refreshUnits = false;
}

CAIMap::~CAIMap() {
    Events()->Destroy();
    FreeGrid(marks);
    FreeGrid(occluders);
    FreeGrid(cells);
    mfc::impl::DestructHeader<mfc::impl::CellRowArrayClass>(&backwardSearch.cellRowsVtable);
    FreeGrid(backwardSearch.levels);
    FreeGrid(backwardSearch.nodes);
    mfc::impl::DestructHeader<mfc::impl::CellRowArrayClass>(&forwardSearch.cellRowsVtable);
    FreeGrid(forwardSearch.levels);
    FreeGrid(forwardSearch.nodes);
    CRegionMap& regions = regionMap;
    // The CRT's eh vector destructor iterator: each grid freed, last to first.
    for (int i = 8; --i >= 0;) FreeGrid(regions.regions[i]);
    for (int i = 8; --i >= 0;) FreeGrid(regions.cells[i]);
    for (int i = 8; --i >= 0;) FreeGrid(regions.edges[i]);
    regions.local.Destruct();
    FreeGrid(regions.backward.records);
    FreeGrid(regions.forward.records);
    for (int list = 5; list >= 0; --list) mfc::impl::DestructArrayOf<mfc::impl::AreaListClass>(areaLists[list]);
    FreeGrid(waterDepth);
    bridgeHeights.Destroy();
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(climbSmall);
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(climbLarge);
}

void CAIMap::Reset() {
    this->~CAIMap();
    new (this) CAIMap();
}
