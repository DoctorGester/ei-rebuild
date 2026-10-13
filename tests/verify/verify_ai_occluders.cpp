// Checks for the occluder words (src/game/ai_occluders.cpp). Ours only (they allocate and free
// sector records): an added occluder leaves the word occupied (bit 8 clear); a removal from a
// word that held one occluder inline leaves it empty.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/sector_map.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool Inside(const CAIMap* map, const FootprintCell* c) {
    return c->x >= 0 && c->y >= 0 && c->x < map->width && c->y < map->height;
}
} // namespace

// 005B61B0.
static bool __fastcall AddOccluderVerify(CAIMap* self, void*, FootprintCell* cell) {
    bool result = self->AddOccluderRecord(cell);
    bool ok = result == Inside(self, cell);
    if (ok && result && cell->low < cell->high) ok = !(self->occluders[cell->y][cell->x] & 0x100);
    ei::verify::Check("CAIMap::AddOccluderRecord", true, ok, "(%d,%d) -> %d", cell->x, cell->y, result);
    return result;
}
EI_VERIFY(0x005B61B0, AddOccluderVerify, 0, s_unused);

// 005B6380.
static bool __fastcall ClearOccluderVerify(CAIMap* self, void*, FootprintCell* cell) {
    bool inline1 = Inside(self, cell) && !(self->occluders[cell->y][cell->x] & 0x180);
    bool result = self->ClearOccluderRecord(cell);
    bool ok = true;
    if (inline1 && cell->low < cell->high) ok = result && (self->occluders[cell->y][cell->x] & 0x100);
    ei::verify::Check("CAIMap::ClearOccluderRecord", true, ok, "(%d,%d) -> %d", cell->x, cell->y, result);
    return result;
}
EI_VERIFY(0x005B6380, ClearOccluderVerify, 0, s_unused);

// ---- The sectors' record pools (sector_map.cpp). Reached from the two functions above (zone
// loads: cells under several objects) and from the sector array's SetSize (CAIMap::Load).

// 005BFE70: allocates, so ours only. A free record is taken from the head of the free list,
// otherwise the array grows by one and its last index is returned.
static int __fastcall NewRecordVerify(Sector* sector) {
    int freeHead = sector->freeOccluder;
    int size = sector->occluders.size;
    int result = sector->NewOccluder();
    int sizeAfter = sector->occluders.size;
    bool ok = freeHead >= 0 ? result == freeHead && sizeAfter == size : result == size && sizeAfter == size + 1;
    ei::verify::Check("Sector::NewOccluder", true, ok, "free %d size %d -> %d", freeHead, size, result);
    return result;
}
EI_VERIFY(0x005BFE70, NewRecordVerify, 0, s_unused);

namespace {
// The pool's state: the free head and every record.
struct PoolState {
    int freeOccluder;
    std::vector<SectorOccluder> records;

    explicit PoolState(const Sector* sector)
        : freeOccluder(sector->freeOccluder),
          records(sector->occluders.data, sector->occluders.data + sector->occluders.size) {}
    void Restore(Sector* sector) const {
        sector->freeOccluder = freeOccluder;
        if (!records.empty())
            std::memcpy(sector->occluders.data, records.data(), records.size() * sizeof(SectorOccluder));
    }
    bool operator==(const PoolState& o) const {
        return freeOccluder == o.freeOccluder && records.size() == o.records.size() &&
               (records.empty() ||
                std::memcmp(records.data(), o.records.data(), records.size() * sizeof(SectorOccluder)) == 0);
    }
};
} // namespace

// 005BFFD0: MOV EAX,[ESP+4]; PUSH EBX (5). The original, its result and pool kept, the pool put
// back, ours; results and pools compared.
static int(__fastcall* s_removeRecord)(Sector*, void*, int, int, int, int, int);
static int __fastcall RemoveRecordVerify(Sector* sector, void*, int head, int bottom, int top, int kind,
                                         int transparency) {
    PoolState before(sector);
    int expected = s_removeRecord(sector, nullptr, head, bottom, top, kind, transparency);
    PoolState afterOriginal(sector);
    before.Restore(sector);
    int actual = sector->RemoveOccluder(head, bottom, top, kind, transparency);
    ei::verify::Check("Sector::RemoveOccluder", true, expected == actual && afterOriginal == PoolState(sector),
                      "head %d (%d..%d kind %d): %d vs %d", head, bottom, top, kind, expected, actual);
    return actual;
}
EI_VERIFY(0x005BFFD0, RemoveRecordVerify, 5, s_removeRecord);

// 005C0060: MOV EDX,[ECX+44]; MOV EAX,[ESP+4] (7). The same way.
static void(__fastcall* s_freeRecord)(Sector*, void*, int);
static void __fastcall FreeRecordVerify(Sector* sector, void*, int index) {
    PoolState before(sector);
    s_freeRecord(sector, nullptr, index);
    PoolState afterOriginal(sector);
    before.Restore(sector);
    sector->FreeOccluder(index);
    ei::verify::Check("Sector::FreeOccluder", true, afterOriginal == PoolState(sector), "index %d", index);
}
EI_VERIFY(0x005C0060, FreeRecordVerify, 7, s_freeRecord);

// 005BFE00: ours only (it frees the records and the lists' blocks): everything empty afterwards.
static void __fastcall SectorClearVerify(Sector* sector) {
    sector->Clear();
    bool ok = sector->freeOccluder == -1 && sector->occluders.size == 0 && sector->occluders.maxSize == 0 &&
              !sector->occluders.data && sector->objects.count == 0 && !sector->objects.head &&
              !sector->objects.tail && !sector->objects.blocks && sector->units.count == 0 && !sector->units.head &&
              !sector->units.tail && !sector->units.blocks;
    ei::verify::Check("Sector::Clear", true, ok, "sector %p", sector);
}
EI_VERIFY(0x005BFE00, SectorClearVerify, 0, s_unused);

// 0059DF90: PUSH -1; PUSH imm32 (7). The original constructs scratch sectors (it allocates
// nothing: empty lists and arrays), ours the real ones; every byte compared.
static void(__stdcall* s_constructSectors)(Sector*, int);
static void __stdcall ConstructSectorsVerify(Sector* sectors, int count) {
    std::vector<unsigned char> scratch(sizeof(Sector) * static_cast<unsigned>(count > 0 ? count : 0), 0xCD);
    if (count > 0) s_constructSectors(reinterpret_cast<Sector*>(scratch.data()), count);
    ConstructSectors(sectors, count);
    ei::verify::Check("ConstructSectors", true,
                      count <= 0 || std::memcmp(scratch.data(), static_cast<void*>(sectors), scratch.size()) == 0,
                      "%d sectors", count);
}
EI_VERIFY(0x0059DF90, ConstructSectorsVerify, 7, s_constructSectors);

// 005BF7D0: PUSH ESI; PUSH EDI; MOV EDI,ECX; MOV EDX,1 (9). Pure. Every FindUnitsInRadius.
static int(__fastcall* s_radiusFind)(const RadiusTree*, void*, float);
static int __fastcall RadiusFindVerify(const RadiusTree* self, void*, float radius) {
    int expected = s_radiusFind(self, nullptr, radius);
    ei::verify::Check("RadiusTree::Find", expected, self->Find(radius), "radius %g", static_cast<double>(radius));
    return expected;
}
EI_VERIFY(0x005BF7D0, RadiusFindVerify, 9, s_radiusFind);
