// The CAIMap constructor (005B3C30) and its reset (005B5E10: every member destroyed in reverse
// order, then the constructor run again in place). The members are built through their own
// constructors (forwarders); the inlined ones (CArrays, CLists, the dirty rectangle) are stores.
// The original's exception frames only matter when a member constructor throws, which ends the
// game either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include "originals_library.h"
#include "originals_ai.h"

namespace {

// Constructors of the members (thiscall).
__declspec(naked) void __fastcall ConstructBridgeMap(void*, void*) EI_ORIGINAL(0x005BCCD0)
__declspec(naked) void __fastcall ConstructSectorArray(void*, void*) EI_ORIGINAL(0x005BD1C0)
void __fastcall ConstructWaterGrid(void* a0, void*) { SearchPartConstructOriginal(a0); }
__declspec(naked) void __fastcall ConstructUnitLayer(CUnitLayer*, void*) EI_ORIGINAL(0x005B4110)
__declspec(naked) void __fastcall ConstructRegionMap(CRegionMap*, void*) EI_ORIGINAL(0x005AF3D0)
__declspec(naked) void __fastcall NodeGridConstruct(void*, void*, int, int) EI_ORIGINAL(0x005BCC50)
__declspec(naked) void __fastcall ByteGridConstruct(void*, void*, int, int) EI_ORIGINAL(0x005BCBD0)
__declspec(naked) void __fastcall ConstructCellRowArray(void*, void*) EI_ORIGINAL(0x005BD1E0)
__declspec(naked) void __fastcall ConstructLocalSearch(LocalSearch*, void*, int) EI_ORIGINAL(0x005B3FF0)
__declspec(naked) void __fastcall CellGridConstruct(void*, void*, int, int) EI_ORIGINAL(0x005BCEE0)
__declspec(naked) void __fastcall OccluderGridConstruct(void*, void*, int, int) EI_ORIGINAL(0x005BCF00)
__declspec(naked) void __fastcall ConstructEvents(AIEventTable*, void*) EI_ORIGINAL(0x005B3F40)
// 005B40F0: resets the dirty rectangle (+4). 005B4150: fills the area lists (ai_area_lists.cpp).
__declspec(naked) void __fastcall ResetDirty(void*, void*) EI_ORIGINAL(0x005B40F0)
__declspec(naked) void __fastcall Initialise(void*, void*) EI_ORIGINAL(0x005B4150)

// Destructors of the members (thiscall).
__declspec(naked) void __fastcall DestroyEvents(AIEventTable*, void*) EI_ORIGINAL(0x005C91F0)
__declspec(naked) void __fastcall DestroyByteGrid(void*, void*) EI_ORIGINAL(0x00598A80)
__declspec(naked) void __fastcall DestroyOccluderGrid(void*, void*) EI_ORIGINAL(0x00598B00)
__declspec(naked) void __fastcall DestroyCellGrid(void*, void*) EI_ORIGINAL(0x005BD010)
__declspec(naked) void __fastcall DestroyCellRowArray(void*, void*) EI_ORIGINAL(0x00599180)
__declspec(naked) void __fastcall DestroyNodeGrid(void*, void*) EI_ORIGINAL(0x00598A60)
__declspec(naked) void __fastcall DestroyLocalSearch(LocalSearch*, void*) EI_ORIGINAL(0x0058AA60)
__declspec(naked) void __fastcall DestroyBlockSearch(BlockSearch*, void*) EI_ORIGINAL(0x00598AA0)
__declspec(naked) void __fastcall DestroyNodeList(void*, void*) EI_ORIGINAL(0x005995E0)
__declspec(naked) void __fastcall DestroyAreaList(void*, void*) EI_ORIGINAL(0x005997A0)
__declspec(naked) void __fastcall DestroyCellSet(FootprintCellSet*, void*) EI_ORIGINAL(0x005995C0)
__declspec(naked) void __fastcall DestroySectorArray(void*, void*) EI_ORIGINAL(0x00599330)
__declspec(naked) void __fastcall DestroyBridgeMap(void*, void*) EI_ORIGINAL(0x005984A0)
__declspec(naked) void __fastcall DestroyTerrainTypes(void*, void*) EI_ORIGINAL(0x00599780)
__declspec(naked) void __fastcall DestroyClimbTable(void*, void*) EI_ORIGINAL(0x004600D0)
// 006E859F: the CRT's `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestroy(void* a0, unsigned a1, int a2, std::uintptr_t a3) {
    EhVectorDestructorIterator(a0, a1, a2, reinterpret_cast<void*>(a3));
}

constexpr std::uint32_t kArrayVtable = 0x0073C3D0;      // climbLarge, climbSmall
constexpr std::uint32_t kAreaListVtable = 0x0073F9E0;   // areaLists

void ConstructUnitLayerOf(CAIMap* self, CUnitLayer* layer, std::uintptr_t vtable) {
    ConstructUnitLayer(layer, nullptr);
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
    ConstructBridgeMap(self->bridgeHeights, nullptr);
    ConstructSectorArray(&self->sectorMap.sectors, nullptr);
    CSectorMap& sectors = self->sectorMap;
    sectors.vtable = reinterpret_cast<void*>(CSectorMap::kVtableAddress);
    sectors.aiMap = nullptr;
    sectors.height = 0;
    sectors.added = 0;
    sectors.width = 0;
    self->footprint.cells = nullptr;
    self->footprint.growBy = 0;
    self->footprint.capacity = 0;
    self->footprint.count = 0;
    self->footprint.hashed = true;
    self->bridges.cells = nullptr;
    self->bridges.growBy = 0;
    self->bridges.capacity = 0;
    self->bridges.count = 0;
    self->bridges.hashed = true;
    self->waterWidth = 1;
    self->waterHeight = 1;
    ConstructWaterGrid(&self->waterBlock, nullptr);
    for (mfc::CArray<AreaOffset>& list : self->areaLists) list.Construct(kAreaListVtable);
    ConstructUnitLayerOf(self, &self->unitLayerSmall, CUnitLayer::kSmallVtableAddress);
    ConstructUnitLayerOf(self, &self->unitLayerLarge, CUnitLayer::kLargeVtableAddress);
    ConstructRegionMap(&self->regionMap, nullptr);
    NodeGridConstruct(&self->forwardSearch.nodes, nullptr, 1, 1);
    ByteGridConstruct(&self->forwardSearch.levels, nullptr, 1, 1);
    ConstructCellRowArray(&self->forwardSearch.cellRowsVtable, nullptr);
    self->forwardSearch.map = nullptr;
    ConstructLocalSearch(&self->backwardSearch, nullptr, 0);
    CellGridConstruct(&self->cellBlock, nullptr, 1, 1);
    OccluderGridConstruct(&self->occluderBlock, nullptr, 1, 1);
    ByteGridConstruct(&self->marks, nullptr, 1, 1);
    ConstructEvents(self->Events(), nullptr);
    self->loaded = false;
    self->batching = false;
    ResetDirty(&self->dirtyMinX, nullptr);
    Initialise(self, nullptr);
    self->lastFootprintOwner = 0;
    self->refreshUnits = false;
    return self;
}

// 005B5E10 (thiscall).
void __fastcall ResetAIMap(CAIMap* self) {
    DestroyEvents(self->Events(), nullptr);
    DestroyByteGrid(&self->marks, nullptr);
    DestroyOccluderGrid(&self->occluderBlock, nullptr);
    DestroyCellGrid(&self->cellBlock, nullptr);
    DestroyCellRowArray(&self->backwardSearch.cellRowsVtable, nullptr);
    DestroyByteGrid(&self->backwardSearch.levels, nullptr);
    DestroyNodeGrid(&self->backwardSearch.nodes, nullptr);
    DestroyCellRowArray(&self->forwardSearch.cellRowsVtable, nullptr);
    DestroyByteGrid(&self->forwardSearch.levels, nullptr);
    DestroyNodeGrid(&self->forwardSearch.nodes, nullptr);
    CRegionMap& regions = self->regionMap;
    VectorDestroy(regions.regions, 0x10, 8, kRegionGridDestructor);
    VectorDestroy(regions.cells, 0x10, 8, kCellGridDestructor);
    VectorDestroy(regions.edges, 0x10, 8, kEdgeGridDestructor);
    DestroyLocalSearch(&regions.local, nullptr);
    DestroyBlockSearch(&regions.backward, nullptr);
    DestroyBlockSearch(&regions.forward, nullptr);
    DestroyNodeList(&self->unitLayerLarge.nodes, nullptr);
    DestroyNodeList(&self->unitLayerSmall.nodes, nullptr);
    for (int list = 5; list >= 0; --list) DestroyAreaList(&self->areaLists[list], nullptr);
    DestroyByteGrid(&self->waterBlock, nullptr);
    DestroyCellSet(&self->bridges, nullptr);
    DestroyCellSet(&self->footprint, nullptr);
    self->sectorMap.vtable = reinterpret_cast<void*>(CSectorMap::kVtableAddress);
    DestroySectorArray(&self->sectorMap.sectors, nullptr);
    DestroyBridgeMap(self->bridgeHeights, nullptr);
    DestroyTerrainTypes(&self->materials, nullptr);
    DestroyClimbTable(&self->climbSmall, nullptr);
    DestroyClimbTable(&self->climbLarge, nullptr);
    if (self) ConstructAIMap(self);
}

EI_REPLACE(0x005B3C30, ConstructAIMap);
EI_REPLACE(0x005B5E10, ResetAIMap);
