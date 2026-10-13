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

// FindUnitsInRadius only writes its output list: run the original into a scratch list and compare.
// 005C0BD0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes).
static void(__fastcall* s_findUnits)(CSectorMap*, void*, const Vector3*, mfc::CTypedPtrList<CEngineObject>*, float);

static void __fastcall FindUnitsVerify(CSectorMap* self, void*, const Vector3* pos,
                                       mfc::CTypedPtrList<CEngineObject>* result, float radius) {
    mfc::CTypedPtrList<CEngineObject> expected{};
    expected.vtable = result->vtable;
    expected.blockSize = result->blockSize;
    s_findUnits(self, nullptr, pos, &expected, radius);
    self->FindUnitsInRadius(pos, result, radius);
    bool same = expected.count == result->count;
    for (auto *a = expected.head, *b = result->head; same && a; a = a->next, b = b->next) same = b && a->data == b->data;
    ei::verify::Check("CSectorMap::FindUnitsInRadius", true, same, "r %g: %d vs %d units", radius, expected.count,
                      result->count);
    if (expected.blocks) expected.RemoveAll();
}
EI_VERIFY(0x005C0BD0, FindUnitsVerify, 6, s_findUnits);

static bool __fastcall MoveVerify(CSectorMap* self, void*, CEngineObject* object, const Vector3* position) {
    Vector3 target = *position;
    bool result = self->Move(object, position);
    auto* u = static_cast<CUnit*>(object);
    const Vector3& now = *reinterpret_cast<const Vector3*>(reinterpret_cast<const char*>(object) + 0x1C);
    bool ok = !result || (now.x == target.x && now.y == target.y && now.z == target.z &&
                          InList(self, object, u->sectorCellX, u->sectorCellY));
    ei::verify::Check("CSectorMap::Move", true, ok, "result %d cell (%d,%d)", result, u->sectorCellX, u->sectorCellY);
    return result;
}
EI_VERIFY(0x005C11D0, MoveVerify, 0, s_unused);

// 005C0930: MOV EDX,[ESP+4]; PUSH ESI (5 bytes). Pure.
static mfc::CTypedPtrList<CEngineObject>*(__fastcall* s_unitsAt)(CSectorMap*, void*, int, int);
static mfc::CTypedPtrList<CEngineObject>* __fastcall UnitsAtVerify(CSectorMap* self, void*, int sx, int sy) {
    auto* expected = s_unitsAt(self, nullptr, sx, sy);
    auto* actual = self->UnitsAt(sx, sy);
    ei::verify::Check("CSectorMap::UnitsAt", expected, actual, "(%d,%d)", sx, sy);
    return actual;
}
EI_VERIFY(0x005C0930, UnitsAtVerify, 5, s_unitsAt);

// 005C06F0 (A-W11): ours only (it frees and allocates the sectors). Afterwards the size is cells / 32
// each way, the array holds width * height constructed sectors (vtable 0073FADC) and the AI map
// is set. Every zone load (CAIMap::Load).
static void __fastcall InitVerify(CSectorMap* self, void*, int cellsX, int cellsY, CAIMap* map) {
    self->Init(cellsX, cellsY, map);
    bool ok = self->width == cellsX / 32 && self->height == cellsY / 32 && self->added == 0 &&
              self->sectors.size == self->width * self->height && self->sectors.maxSize >= self->sectors.size &&
              self->aiMap == map;
    for (int i = 0; ok && i < self->sectors.size; ++i)
        ok = *reinterpret_cast<const std::uintptr_t*>(&self->sectors[i]) == 0x0073FADC;
    ei::verify::Check("CSectorMap::Init", true, ok, "%d x %d cells: %d sectors", cellsX, cellsY, self->sectors.size);
}
EI_VERIFY(0x005C06F0, InitVerify, 0, s_unused);

// A-W13. 0058A700 / 0058A780 (the destructor): the sectors and their storage are freed, so ours
// only. Checked: both vtables are the map's own; the deleting one returns the object. Reached only
// when the AI map's construction unwinds.
static void __fastcall DestructBodyVerify(CSectorMap* self) {
    self->DestructBody();
    bool ok = self->vtable == reinterpret_cast<void*>(CSectorMap::kVtableAddress) &&
              self->sectors.vtable == reinterpret_cast<void*>(CSectorMap::kSectorArrayVtable);
    ei::verify::Check("CSectorMap::DestructBody", true, ok, "%d sectors", self->sectors.size);
}
EI_VERIFY(0x0058A700, DestructBodyVerify, 0, s_unused);

static CSectorMap* __fastcall DeletingDestructVerify(CSectorMap* self, void*, unsigned flags) {
    CSectorMap* result = self->DeletingDestruct(flags);
    ei::verify::Check("CSectorMap::DeletingDestruct", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x0058A780, DeletingDestructVerify, 0, s_unused);
