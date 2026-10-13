// Checks for the AI map's grids and member constructors (src/game/ai_grids.cpp). The allocators
// and constructors are compared on a probe grid the original fills (then freed): sizes, the row
// table's layout and the elements the original initialises. The frees and the event table are
// ours only (they free memory). Reached by every map load (CAIMap::Load, the region map's Attach)
// and the AI map's constructor at start-up; BuildOpenMarks by every map load.
#include "verify.h"

#include "../../src/game/ai_grids.h"
#include "../../src/game/ai_unit_layer.h"
#include "../../src/game/unit_ai.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

// rows[i] == data + i * width * perCell for every row.
template <typename T>
bool RowsFit(const SearchGrid<T>& grid, int perCell = 1) {
    if (!grid.data || !grid.rows) return false;
    for (int i = 0; i < grid.height; ++i)
        if (grid.rows[i] != grid.data + i * grid.width * perCell) return false;
    return true;
}

template <typename T, typename Same>
bool SameCells(const SearchGrid<T>& a, const SearchGrid<T>& b, int perCell, Same same) {
    if (a.width != b.width || a.height != b.height) return false;
    int count = a.width * a.height * perCell;
    for (int i = 0; i < count; ++i)
        if (!same(a.data[i], b.data[i])) return false;
    return true;
}

template <typename T>
void FreeProbe(SearchGrid<T>& probe) {
    gamecrt::operator_delete(probe.data);
    gamecrt::operator_delete(probe.rows);
}

// The original allocates a probe of the same size, ours the real grid.
template <typename T, typename Same>
void CompareAllocate(const char* name, SearchGrid<T>* grid, void(__fastcall* original)(SearchGrid<T>*),
                     void(__fastcall* ours)(SearchGrid<T>*), int perCell, Same same) {
    SearchGrid<T> probe{nullptr, nullptr, grid->width, grid->height};
    original(&probe);
    ours(grid);
    bool ok = RowsFit(probe, perCell) && RowsFit(*grid, perCell) && SameCells(probe, *grid, perCell, same);
    ei::verify::Check(name, true, ok, "%d x %d", grid->width, grid->height);
    FreeProbe(probe);
}

template <typename T>
bool Any(const T&, const T&) {
    return true;
}
template <typename T>
bool Equal(const T& a, const T& b) {
    return a == b;
}
bool SameCell(const AIMapCell& a, const AIMapCell& b) {
    return a.passable == b.passable && a.groundLevel == b.groundLevel && a.flags == b.flags && a.level == b.level;
}
// The bits the constructor sets: 0-4, 7 and 8.
bool SameOccluder(const std::uint32_t& a, const std::uint32_t& b) { return ((a ^ b) & 0x19F) == 0 && (a & 0x19F) == 0x100; }
} // namespace

// 005B1470: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+8] (6).
static void(__fastcall* s_allocateNodes)(SearchGrid<SearchNode>*);
static void __fastcall AllocateNodesVerify(SearchGrid<SearchNode>* grid) {
    CompareAllocate("AllocateNodeGrid", grid, s_allocateNodes, AllocateNodeGrid, 1, Any<SearchNode>);
}
EI_VERIFY(0x005B1470, AllocateNodesVerify, 6, s_allocateNodes);

// 005B14C0: the same bytes (6).
static void(__fastcall* s_allocateBytes)(SearchGrid<std::uint8_t>*);
static void __fastcall AllocateBytesVerify(SearchGrid<std::uint8_t>* grid) {
    CompareAllocate("AllocateByteGrid", grid, s_allocateBytes, AllocateByteGrid, 1, Any<std::uint8_t>);
}
EI_VERIFY(0x005B14C0, AllocateBytesVerify, 6, s_allocateBytes);

// 005B1500 (6).
static void(__fastcall* s_allocateRecords)(SearchGrid<BlockRecord>*);
static void __fastcall AllocateRecordsVerify(SearchGrid<BlockRecord>* grid) {
    CompareAllocate("AllocateBlockRecordGrid", grid, s_allocateRecords, AllocateBlockRecordGrid, 1, Any<BlockRecord>);
}
EI_VERIFY(0x005B1500, AllocateRecordsVerify, 6, s_allocateRecords);

// 005B1550: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESI+8] (7).
static void(__fastcall* s_allocateEdges)(SearchGrid<std::int16_t>*);
static void __fastcall AllocateEdgesVerify(SearchGrid<std::int16_t>* grid) {
    CompareAllocate("AllocateEdgeGrid", grid, s_allocateEdges, AllocateEdgeGrid, 8, Equal<std::int16_t>);
}
EI_VERIFY(0x005B1550, AllocateEdgesVerify, 7, s_allocateEdges);

// 005B15F0 (7).
static void(__fastcall* s_allocateZeroed)(SearchGrid<std::uint8_t>*);
static void __fastcall AllocateZeroedVerify(SearchGrid<std::uint8_t>* grid) {
    CompareAllocate("AllocateZeroedByteGrid", grid, s_allocateZeroed, AllocateZeroedByteGrid, 1, Equal<std::uint8_t>);
}
EI_VERIFY(0x005B15F0, AllocateZeroedVerify, 7, s_allocateZeroed);

// 005B1680 (6).
static void(__fastcall* s_allocateRegions)(SearchGrid<std::uint16_t>*);
static void __fastcall AllocateRegionsVerify(SearchGrid<std::uint16_t>* grid) {
    CompareAllocate("AllocateRegionGrid", grid, s_allocateRegions, AllocateRegionGrid, 1, Any<std::uint16_t>);
}
EI_VERIFY(0x005B1680, AllocateRegionsVerify, 6, s_allocateRegions);

// 005BD030: PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5).
static void(__fastcall* s_allocateCells)(SearchGrid<AIMapCell>*);
static void __fastcall AllocateCellsVerify(SearchGrid<AIMapCell>* grid) {
    CompareAllocate("AllocateCellGrid", grid, s_allocateCells, AllocateCellGrid, 1, SameCell);
}
EI_VERIFY(0x005BD030, AllocateCellsVerify, 5, s_allocateCells);

// 005BD0B0: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESI+8] (7).
static void(__fastcall* s_allocateOccluders)(SearchGrid<std::uint32_t>*);
static void __fastcall AllocateOccludersVerify(SearchGrid<std::uint32_t>* grid) {
    CompareAllocate("AllocateOccluderGrid", grid, s_allocateOccluders, AllocateOccluderGrid, 1, SameOccluder);
}
EI_VERIFY(0x005BD0B0, AllocateOccludersVerify, 7, s_allocateOccluders);

// 005BD010 and its copies: ours only (the block and the table are freed; the grid's fields stay).
static void __fastcall FreeGridVerify(SearchGrid<std::uint8_t>* grid) {
    SearchGrid<std::uint8_t> before = *grid;
    FreeAnyGrid(grid);
    ei::verify::Check("FreeAnyGrid", true, std::memcmp(&before, grid, sizeof(before)) == 0, "%d x %d", before.width,
                      before.height);
}
EI_VERIFY(0x005BD010, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598400, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598420, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598440, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598460, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598480, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598580, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x005985A0, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598A60, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598A80, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598AA0, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x00598B00, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x005B1450, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x005BCC30, FreeGridVerify, 0, s_unused);
EI_VERIFY(0x0058AAB0, FreeGridVerify, 0, s_unused);   // A-W14 (unwind funclets only)

namespace {
// The original constructs a probe, ours the real grid; the probe's result must be itself.
template <typename T, typename Same>
SearchGrid<T>* CompareConstruct(const char* name, SearchGrid<T>* grid, int width, int height,
                                SearchGrid<T>*(__fastcall* original)(SearchGrid<T>*, void*, int, int),
                                SearchGrid<T>*(__fastcall* ours)(SearchGrid<T>*, void*, int, int), Same same) {
    SearchGrid<T> probe{nullptr, nullptr, -1, -1};
    SearchGrid<T>* expected = original(&probe, nullptr, width, height);
    SearchGrid<T>* actual = ours(grid, nullptr, width, height);
    bool ok = expected == &probe && actual == grid && probe.width == width && probe.height == height &&
              RowsFit(probe) && RowsFit(*grid) && SameCells(probe, *grid, 1, same);
    ei::verify::Check(name, true, ok, "%d x %d", width, height);
    FreeProbe(probe);
    return actual;
}
} // namespace

// 005BCBD0: MOV EAX,[ESP+4]; PUSH ESI (5).
static SearchGrid<std::int8_t>*(__fastcall* s_constructBytes)(SearchGrid<std::int8_t>*, void*, int, int);
static SearchGrid<std::int8_t>* __fastcall ConstructBytesVerify(SearchGrid<std::int8_t>* grid, void*, int width,
                                                               int height) {
    return CompareConstruct("ConstructByteGrid", grid, width, height, s_constructBytes, ConstructByteGrid,
                            Any<std::int8_t>);
}
EI_VERIFY(0x005BCBD0, ConstructBytesVerify, 5, s_constructBytes);

// 005BCC50 (5).
static SearchGrid<SearchNode>*(__fastcall* s_constructNodes)(SearchGrid<SearchNode>*, void*, int, int);
static SearchGrid<SearchNode>* __fastcall ConstructNodesVerify(SearchGrid<SearchNode>* grid, void*, int width,
                                                              int height) {
    return CompareConstruct("ConstructNodeGrid", grid, width, height, s_constructNodes, ConstructNodeGrid,
                            Any<SearchNode>);
}
EI_VERIFY(0x005BCC50, ConstructNodesVerify, 5, s_constructNodes);

// 005BCEE0 (5).
static SearchGrid<AIMapCell>*(__fastcall* s_constructCells)(SearchGrid<AIMapCell>*, void*, int, int);
static SearchGrid<AIMapCell>* __fastcall ConstructCellsVerify(SearchGrid<AIMapCell>* grid, void*, int width,
                                                             int height) {
    return CompareConstruct("ConstructCellGrid", grid, width, height, s_constructCells, ConstructCellGrid, SameCell);
}
EI_VERIFY(0x005BCEE0, ConstructCellsVerify, 5, s_constructCells);

// 005BCF00 (5).
static SearchGrid<std::uint32_t>*(__fastcall* s_constructOccluders)(SearchGrid<std::uint32_t>*, void*, int, int);
static SearchGrid<std::uint32_t>* __fastcall ConstructOccludersVerify(SearchGrid<std::uint32_t>* grid, void*,
                                                                     int width, int height) {
    return CompareConstruct("ConstructOccluderGrid", grid, width, height, s_constructOccluders,
                            ConstructOccluderGrid, SameOccluder);
}
EI_VERIFY(0x005BCF00, ConstructOccludersVerify, 5, s_constructOccluders);

// 005AF5A0: PUSH ESI; MOV ESI,ECX; MOV EAX,1 (8).
static void(__fastcall* s_constructRegions)(SearchGrid<std::uint16_t>*);
static void __fastcall ConstructRegionsVerify(SearchGrid<std::uint16_t>* grid) {
    SearchGrid<std::uint16_t> probe{nullptr, nullptr, -1, -1};
    s_constructRegions(&probe);
    ConstructRegionGrid(grid);
    bool ok = probe.width == 1 && probe.height == 1 && grid->width == 1 && grid->height == 1 && RowsFit(probe) &&
              RowsFit(*grid);
    ei::verify::Check("ConstructRegionGrid", true, ok, "%d x %d", grid->width, grid->height);
    FreeProbe(probe);
}
EI_VERIFY(0x005AF5A0, ConstructRegionsVerify, 8, s_constructRegions);

// 005B3FF0: PUSH -1; PUSH imm32 (7). Without an owner the original constructs a probe search and
// the fields it sets are compared; with one, ours only (Attach has its own check).
static LocalSearch*(__fastcall* s_constructSearch)(LocalSearch*, void*, CAIMap*);
static LocalSearch* __fastcall ConstructSearchVerify(LocalSearch* self, void*, CAIMap* owner) {
    if (owner) {
        LocalSearch* result = self->Construct(owner);
        ei::verify::Check("LocalSearch::Construct (owner)", true, result == self && self->map == owner, "%p", owner);
        return result;
    }
    std::vector<unsigned char> scratch(sizeof(LocalSearch), 0xCD);
    auto* probe = reinterpret_cast<LocalSearch*>(scratch.data());
    LocalSearch* expected = s_constructSearch(probe, nullptr, nullptr);
    LocalSearch* actual = self->Construct(nullptr);
    auto sized = [](const LocalSearch* s) {
        return s->nodes.width == 1 && s->nodes.height == 1 && s->levels.width == 1 && s->levels.height == 1 &&
               RowsFit(s->nodes) && RowsFit(s->levels) && !s->map;
    };
    bool ok = expected == probe && actual == self && sized(probe) && sized(self) &&
              std::memcmp(&probe->cellRowsVtable, &self->cellRowsVtable,
                          offsetof(LocalSearch, settledList) - offsetof(LocalSearch, cellRowsVtable)) == 0;
    ei::verify::Check("LocalSearch::Construct", true, ok, "%p", self);
    FreeProbe(probe->nodes);
    FreeProbe(probe->levels);
    return actual;
}
EI_VERIFY(0x005B3FF0, ConstructSearchVerify, 7, s_constructSearch);

// 005B4110: MOV EDX,ECX; PUSH ESI; XOR ESI,ESI (5). The original on a copy (it allocates
// nothing), ours in place; every byte compared.
static CUnitLayer*(__fastcall* s_constructLayer)(CUnitLayer*);
static CUnitLayer* __fastcall ConstructLayerVerify(CUnitLayer* self) {
    std::vector<unsigned char> scratch(sizeof(CUnitLayer));
    std::memcpy(scratch.data(), self, sizeof(CUnitLayer));
    auto* probe = reinterpret_cast<CUnitLayer*>(scratch.data());
    CUnitLayer* expected = s_constructLayer(probe);
    CUnitLayer* actual = self->Construct();
    ei::verify::Check("CUnitLayer::Construct", true,
                      expected == probe && actual == self && std::memcmp(probe, self, sizeof(CUnitLayer)) == 0, "%p",
                      self);
    return actual;
}
EI_VERIFY(0x005B4110, ConstructLayerVerify, 5, s_constructLayer);

// 005B40F0: XOR EAX,EAX; MOV [ECX],0x2710 (8). Pure.
static void(__fastcall* s_resetDirty)(AIDirtyRect*);
static void __fastcall ResetDirtyVerify(AIDirtyRect* self) {
    AIDirtyRect expected;
    s_resetDirty(&expected);
    self->Reset();
    ei::verify::Check("AIDirtyRect::Reset", true, std::memcmp(&expected, self, sizeof(expected)) == 0, "%d %d %d %d",
                      self->minX, self->maxX, self->minY, self->maxY);
}
EI_VERIFY(0x005B40F0, ResetDirtyVerify, 8, s_resetDirty);

// 005BDD50: MOV EDX,[ESP+4]; MOV ECX,[ECX] (6). Pure.
static AITerrainType*(__fastcall* s_terrainTypeAt)(AITerrainType**, void*, int);
static AITerrainType* __fastcall TerrainTypeAtVerify(AITerrainType** types, void*, int index) {
    AITerrainType* expected = s_terrainTypeAt(types, nullptr, index);
    ei::verify::Check("TerrainTypeAt", true, expected == TerrainTypeAt(types, nullptr, index), "index %d", index);
    return expected;
}
EI_VERIFY(0x005BDD50, TerrainTypeAtVerify, 6, s_terrainTypeAt);

// A-W13. 005CD490: another copy of the grids' destructor (the event table's cells).
EI_VERIFY(0x005CD490, FreeGridVerify, 0, s_unused);

// 005CD4B0: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+8] (6). The event table's cells, as the other
// allocations (the cells stay as allocated). Reached by the event table's resize (every zone load).
static void(__fastcall* s_allocateEvents)(SearchGrid<AIEventCell>*);
static void __fastcall AllocateEventsVerify(SearchGrid<AIEventCell>* grid) {
    CompareAllocate("AllocateEventGrid", grid, s_allocateEvents, AllocateEventGrid, 1, Any<AIEventCell>);
}
EI_VERIFY(0x005CD4B0, AllocateEventsVerify, 6, s_allocateEvents);

// 005CD3A0: PUSH EBX; MOV EBX,[ESP+0xC] (5). The original resizes a probe of the grid's size (its
// own blocks), ours the real grid; both end with the size asked and fitting rows, and a call for
// the size the grid has leaves the pointers alone. The visibility marks (the AI's sight checks).
static void(__fastcall* s_resizeBytes)(SearchGrid<std::uint8_t>*, void*, int, int);
static void __fastcall ResizeBytesVerify(SearchGrid<std::uint8_t>* grid, void*, int width, int height) {
    SearchGrid<std::uint8_t> before = *grid;
    SearchGrid<std::uint8_t> probe{nullptr, nullptr, grid->width, grid->height};
    AllocateByteGrid(&probe);
    SearchGrid<std::uint8_t> probeBefore = probe;
    s_resizeBytes(&probe, nullptr, width, height);
    ResizeByteGrid(grid, nullptr, width, height);
    bool same = before.width == width && before.height == height;
    bool ok = probe.width == width && probe.height == height && grid->width == width && grid->height == height &&
              RowsFit(probe) && RowsFit(*grid) && (!same || (probe.data == probeBefore.data && grid->data == before.data &&
                                                             grid->rows == before.rows));
    ei::verify::Check("ResizeByteGrid", true, ok, "%d x %d -> %d x %d", before.width, before.height, width, height);
    FreeProbe(probe);
}
EI_VERIFY(0x005CD3A0, ResizeBytesVerify, 5, s_resizeBytes);

// 005CD410: PUSH EBP; MOV EBP,[ESP+8] (5). The original copies into a probe, ours into the real
// grid; the sizes and every cell compared with the source. Growing the visibility marks.
static void(__fastcall* s_copyBytes)(SearchGrid<std::uint8_t>*, void*, const SearchGrid<std::uint8_t>*);
static void __fastcall CopyBytesVerify(SearchGrid<std::uint8_t>* grid, void*, const SearchGrid<std::uint8_t>* source) {
    SearchGrid<std::uint8_t> probe{nullptr, nullptr, -1, -1};
    s_copyBytes(&probe, nullptr, source);
    CopyByteGrid(grid, nullptr, source);
    bool ok = RowsFit(probe) && RowsFit(*grid) && SameCells(probe, *grid, 1, Equal<std::uint8_t>) &&
              SameCells(*source, *grid, 1, Equal<std::uint8_t>) && grid->data != source->data;
    ei::verify::Check("CopyByteGrid", true, ok, "%d x %d", source->width, source->height);
    FreeProbe(probe);
}
EI_VERIFY(0x005CD410, CopyBytesVerify, 5, s_copyBytes);

// 005C4BA0 CopyNodeGrid (A-W14; PUSH EBP; MOV EBP,[ESP+8]: 5 bytes): the original copies into a
// probe, ours into the real grid; both must hold the source's nodes (compared by their bytes: the
// copies are plain memory copies) in a block of their own. No caller is left: the original
// Recenter 005C25D0 is rewritten and copies the window itself.
static void(__fastcall* s_copyNodes)(SearchGrid<SearchNode>*, void*, const SearchGrid<SearchNode>*);
static void __fastcall CopyNodesVerify(SearchGrid<SearchNode>* grid, void*, const SearchGrid<SearchNode>* source) {
    SearchGrid<SearchNode> probe{nullptr, nullptr, -1, -1};
    s_copyNodes(&probe, nullptr, source);
    CopyNodeGrid(grid, nullptr, source);
    auto sameBytes = [](const SearchNode& a, const SearchNode& b) { return std::memcmp(&a, &b, sizeof(SearchNode)) == 0; };
    bool ok = RowsFit(probe) && RowsFit(*grid) && SameCells(probe, *grid, 1, sameBytes) &&
              SameCells(*source, *grid, 1, sameBytes) && grid->data != source->data;
    ei::verify::Check("CopyNodeGrid", true, ok, "%d x %d", source->width, source->height);
    FreeProbe(probe);
}
EI_VERIFY(0x005C4BA0, CopyNodesVerify, 5, s_copyNodes);

// 0058A8A0 / 0058A900 (the unit layers' destructors): the list's blocks are freed, so ours only.
// Checked: the list empty with no blocks. Reached only when the AI map's construction unwinds.
static void __fastcall UnitLayerDestructVerify(CUnitLayer* self) {
    self->Destruct();
    bool ok = self->head == nullptr && self->tail == nullptr && self->count == 0 && self->free == nullptr &&
              self->blocks == nullptr;
    ei::verify::Check("CUnitLayer::Destruct", true, ok, "count %d", self->count);
}
EI_VERIFY(0x0058A8A0, UnitLayerDestructVerify, 0, s_unused);
EI_VERIFY(0x0058A900, UnitLayerDestructVerify, 0, s_unused);

namespace {
bool EventTableEmpty(const AIEventTable* table) {
    if (table->tick != -1 || table->width != 1 || table->height != 1 || !table->cells || table->rows[0] != table->cells)
        return false;
    for (const auto& slot : table->slots)
        for (const AIEvent* head : slot)
            if (head) return false;
    for (const AIEvent* head : table->cells[0].bySide)
        if (head) return false;
    return true;
}
} // namespace

// 005B3F40 / 005C9250: ours only (they allocate and free the grid): empty afterwards.
static AIEventTable* __fastcall ConstructEventsVerify(AIEventTable* self) {
    AIEventTable* result = self->Construct();
    ei::verify::Check("AIEventTable::Construct", true, result == self && EventTableEmpty(self), "%p", self);
    return result;
}
EI_VERIFY(0x005B3F40, ConstructEventsVerify, 0, s_unused);

static void __fastcall ClearEventsVerify(AIEventTable* self) {
    self->Clear();
    ei::verify::Check("AIEventTable::Clear", true, EventTableEmpty(self), "%p", self);
}
EI_VERIFY(0x005C9250, ClearEventsVerify, 0, s_unused);

// 005C91F0: ours only (frees the grid). Every event of the wheel is on the free list afterwards,
// ahead of what was there.
static void __fastcall DestroyEventsVerify(AIEventTable* self) {
    AIEvent*& freeEvents = *reinterpret_cast<AIEvent**>(std::uintptr_t(0x007C53F0));
    AIEvent* oldHead = freeEvents;
    int queued = 0;
    for (const auto& slot : self->slots)
        for (const AIEvent* head : slot)
            for (const AIEvent* event = head; event; event = event->nextInSlot) ++queued;
    self->Destroy();
    int freed = 0;
    const AIEvent* event = freeEvents;
    for (; event && event != oldHead && freed <= queued; event = event->next) ++freed;
    ei::verify::Check("AIEventTable::Destroy", queued, freed, "old head %p reached %d", oldHead, event == oldHead);
}
EI_VERIFY(0x005C91F0, DestroyEventsVerify, 0, s_unused);

// 005B8620: PUSH EBP; MOV EBP,ESP; SUB ESP,0x10 (6). The original fills the marks, they are kept
// and overwritten, ours fills them again; compared.
static void(__fastcall* s_buildOpenMarks)(CAIMap*);
static void __fastcall BuildOpenMarksVerify(CAIMap* self) {
    s_buildOpenMarks(self);
    int count = self->materialCount > 0 ? self->materialCount : 0;
    std::vector<std::uint32_t> expected(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        expected[static_cast<std::size_t>(i)] = self->materials[i].openMarks;
        self->materials[i].openMarks = 0xDEADBEEF;
    }
    self->BuildOpenMarks();
    int bad = -1;
    for (int i = 0; i < count; ++i)
        if (self->materials[i].openMarks != expected[static_cast<std::size_t>(i)]) bad = i;
    ei::verify::Check("CAIMap::BuildOpenMarks", -1, bad, "%d types", count);
}
EI_VERIFY(0x005B8620, BuildOpenMarksVerify, 6, s_buildOpenMarks);
