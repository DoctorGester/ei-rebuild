// The CAIMap constructor (005B3C30) and its reset (005B5E10: every member destroyed in reverse
// order, then the constructor run again in place). The members are built through their own
// constructors (forwarders); the inlined ones (CArrays, CLists, the dirty rectangle) are stores.
// The original's exception frames only matter when a member constructor throws, which ends the
// game either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_grids.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"

#include <cstdint>
#include "originals_library.h"
#include "originals_ai.h"
#include "unit_ai.h"
#include "lib/mfc_list.h"

namespace {

// Constructors of the members (thiscall).
// 005B40F0: resets the dirty rectangle (+4).
__declspec(naked) void __fastcall ResetDirty(void*, void*) EI_ORIGINAL(0x005B40F0)

// Destructors of the members (thiscall).
// 006E859F: the CRT's `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestroy(void* a0, unsigned a1, int a2, std::uintptr_t a3) {
    libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3));
}

constexpr std::uint32_t kArrayVtable = std::uint32_t(0x0073C3D0);      // climbLarge, climbSmall
constexpr std::uint32_t kAreaListVtable = std::uint32_t(0x0073F9E0);   // areaLists

void ConstructUnitLayerOf(CAIMap* self, CUnitLayer* layer, std::uintptr_t vtable) {
    layer->Construct();
    layer->map = self;
    ei::SetVtable(layer, vtable);
}

} // namespace

// 005B3C30 (thiscall). Returns this.
CAIMap* __fastcall ConstructAIMap(CAIMap* self) {
    self->dirtyMinX = 10000;
    self->dirtyMaxX = 0;
    self->dirtyMinY = 10000;
    self->dirtyMaxY = 0;
    self->climbLarge.Construct(kArrayVtable);
    self->climbSmall.Construct(kArrayVtable);
    self->materials.Construct();
    self->bridgeHeights.Construct(100);   // 005BCCD0
    mfc::impl::ConstructArrayOf<mfc::impl::SectorArrayClass>(self->sectorMap.sectors);
    CSectorMap& sectors = self->sectorMap;
    sectors.vtable = reinterpret_cast<void*>(CSectorMap::kVtableAddress);
    sectors.aiMap = nullptr;
    sectors.height = 0;
    sectors.added = 0;
    sectors.width = 0;
    self->footprint.cells.Construct();
    self->footprint.hashed = true;
    self->bridges.cells.Construct();
    self->bridges.hashed = true;
    self->waterWidth = 1;
    self->waterHeight = 1;
    AllocateGrid(self->WaterGrid());
    for (mfc::CArray<AreaOffset>& list : self->areaLists) list.Construct(kAreaListVtable);
    ConstructUnitLayerOf(self, &self->unitLayerSmall, CUnitLayer::kSmallVtableAddress);
    ConstructUnitLayerOf(self, &self->unitLayerLarge, CUnitLayer::kLargeVtableAddress);
    ConstructRegionMap(&self->regionMap);   // 005AF3D0
    self->forwardSearch.nodes.Construct(1, 1);
    reinterpret_cast<SearchGrid<std::int8_t>*>(&self->forwardSearch.levels)->Construct(1, 1);   // 005BCBD0
    mfc::impl::ConstructHeader<mfc::impl::CellRowArrayClass>(&self->forwardSearch.cellRowsVtable);
    self->forwardSearch.map = nullptr;
    self->backwardSearch.Construct(nullptr);
    self->CellGrid().Construct(1, 1);
    self->OccluderGrid().Construct(1, 1);
    self->marks.Construct(1, 1);
    self->Events()->Construct();
    self->loaded = false;
    self->batching = false;
    ResetDirty(&self->dirtyMinX, nullptr);
    FillAreaLists(self);
    self->lastFootprintOwner = 0;
    self->refreshUnits = false;
    return self;
}

// 0058A450 (thiscall). The destructor inlines some member destructors the reset calls (same
// effects).
void __fastcall DestructAIMap(CAIMap* self) {
    self->Events()->Destroy();
    FreeGrid(self->marks);
    FreeGrid(self->OccluderGrid());
    FreeGrid(self->CellGrid());
    mfc::impl::DestructHeader<mfc::impl::CellRowArrayClass>(&self->backwardSearch.cellRowsVtable);
    FreeGrid(self->backwardSearch.levels);
    FreeGrid(self->backwardSearch.nodes);
    mfc::impl::DestructHeader<mfc::impl::CellRowArrayClass>(&self->forwardSearch.cellRowsVtable);
    FreeGrid(self->forwardSearch.levels);
    FreeGrid(self->forwardSearch.nodes);
    CRegionMap& regions = self->regionMap;
    VectorDestroy(regions.regions, 0x10, 8, kRegionGridDestructor);
    VectorDestroy(regions.cells, 0x10, 8, kCellGridDestructor);
    VectorDestroy(regions.edges, 0x10, 8, kEdgeGridDestructor);
    regions.local.Destruct();
    FreeGrid(regions.backward.records);
    FreeGrid(regions.forward.records);
    mfc::impl::DestructList<mfc::impl::NodeListClass>(self->unitLayerLarge.nodes);   // 005995E0
    mfc::impl::DestructList<mfc::impl::NodeListClass>(self->unitLayerSmall.nodes);
    for (int list = 5; list >= 0; --list) mfc::impl::DestructArrayOf<mfc::impl::AreaListClass>(self->areaLists[list]);
    FreeGrid(self->WaterGrid());
    self->bridges.cells.FreeData();
    self->footprint.cells.FreeData();
    self->sectorMap.vtable = reinterpret_cast<void*>(CSectorMap::kVtableAddress);
    CSectorMap::DestructSectors(&self->sectorMap.sectors);
    self->bridgeHeights.Destroy();   // 005984A0
    self->materials.FreeData();
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(self->climbSmall);
    mfc::impl::DestructArrayOf<mfc::impl::IntArrayClass>(self->climbLarge);
}

// 005B5E10 (thiscall).
void __fastcall ResetAIMap(CAIMap* self) {
    DestructAIMap(self);
    if (self) ConstructAIMap(self);
}

EI_REPLACE(0x005B3C30, ConstructAIMap);
EI_REPLACE(0x005B5E10, ResetAIMap);
EI_REPLACE(0x0058A450, DestructAIMap);
