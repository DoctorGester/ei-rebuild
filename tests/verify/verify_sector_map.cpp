// The sector map changes lists and the AI map grid, so it cannot run twice. After our version
// runs, check that the object's list membership, flag and cell agree with the result.
#include "verify.h"

#include "../../src/game/sector_map.h"

namespace {
void* s_unused;

bool InList(CSectorMap* map, CEngineObject* o, int cellX, int cellY) {
    int sx = cellX / 32, sy = cellY / 32;
    if (sx < 0 || sy < 0 || sx >= map->width || sy >= map->height) return false;
    Sector& s = map->sectors[map->width * sy + sx];
    return (o->GetClassId() == kClassUnit ? s.units : s.objects).Find(o) != nullptr;
}
} // namespace

static bool __fastcall AddVerify(CSectorMap* self, void*, CEngineObject* object) {
    bool result = self->Add(object);
    auto* u = static_cast<CUnit*>(object);
    bool ok = result ? (InList(self, object, u->sectorCellX, u->sectorCellY) && (object->flags_ & kInSectorMap))
                     : true;
    ei::verify::Check("CSectorMap::Add", true, ok, "result %d cell (%d,%d)", result, u->sectorCellX, u->sectorCellY);
    return result;
}
EI_VERIFY(0x005C0DF0, AddVerify, 0, s_unused);

static bool __fastcall RemoveVerify(CSectorMap* self, void*, CEngineObject* object) {
    auto* u = static_cast<CUnit*>(object);
    int cx = u->sectorCellX, cy = u->sectorCellY;
    bool result = self->Remove(object);
    bool ok = result ? (!InList(self, object, cx, cy) && u->sectorCellX == -1 && u->sectorCellY == -1) : true;
    ei::verify::Check("CSectorMap::Remove", true, ok, "result %d cell (%d,%d)", result, cx, cy);
    return result;
}
EI_VERIFY(0x005C1010, RemoveVerify, 0, s_unused);
