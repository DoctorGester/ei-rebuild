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
