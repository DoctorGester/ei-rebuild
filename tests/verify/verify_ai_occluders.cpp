// Checks for the occluder words (src/game/ai_occluders.cpp). Ours only (they allocate and free
// sector records): an added occluder leaves the word occupied (bit 8 clear); a removal from a
// word that held one occluder inline leaves it empty.
#include "verify.h"

#include "../../src/game/ai_map.h"

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

int __fastcall NewOccluderRecord(std::uint8_t* sector);

// 005BFE70: allocates, so ours only. A free record is taken from the head of the free list,
// otherwise the array grows by one and its last index is returned.
static int __fastcall NewRecordVerify(std::uint8_t* sector) {
    int freeHead = *reinterpret_cast<int*>(sector + 0x3C);
    int size = *reinterpret_cast<int*>(sector + 0x48);
    int result = NewOccluderRecord(sector);
    int sizeAfter = *reinterpret_cast<int*>(sector + 0x48);
    bool ok = freeHead >= 0 ? result == freeHead && sizeAfter == size : result == size && sizeAfter == size + 1;
    ei::verify::Check("NewOccluderRecord", true, ok, "free %d size %d -> %d", freeHead, size, result);
    return result;
}
EI_VERIFY(0x005BFE70, NewRecordVerify, 0, s_unused);
