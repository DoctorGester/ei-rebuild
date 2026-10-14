// Checks for object_small_slots.cpp (A-U2).
// - HasWeaponModels (005391B0) / HasArmourModels (005391E0) / CompanionQuery (0050E460): pure,
//   original and ours compared.
// - CreateServer (005157C0) / DestroyServer (005157E0): ours only; result 1 / no map object, the
//   string array empty.
// Reached by: the server companions' Create / Destroy with every map object on the server (all
// scenarios that spawn: units, unit_create); slots 36 / 37 of the client controller by the
// interface's hero views (ui_*); slot 31 of map objects (its callers are not traced; the counts show).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

namespace {
void* s_unusedCreate;
void* s_unusedDestroy;
} // namespace

// 005391B0: MOV EDX,[ECX+0xAC] (6 bytes).
static int(__fastcall* s_hasWeapons)(CUnitClientSpecific*);
static int __fastcall HasWeaponModelsVerify(CUnitClientSpecific* self) {
    int expected = s_hasWeapons(self);
    int actual = self->HasWeaponModels();
    ei::verify::Check("CUnitClientSpecific::HasWeaponModels", expected, actual, "unit %d", self->unit->id_);
    return actual;
}
EI_VERIFY(0x005391B0, HasWeaponModelsVerify, 6, s_hasWeapons);

// 005391E0: MOV EDX,[ECX+0xA0] (6 bytes).
static int(__fastcall* s_hasArmour)(CUnitClientSpecific*);
static int __fastcall HasArmourModelsVerify(CUnitClientSpecific* self) {
    int expected = s_hasArmour(self);
    int actual = self->HasArmourModels();
    ei::verify::Check("CUnitClientSpecific::HasArmourModels", expected, actual, "unit %d", self->unit->id_);
    return actual;
}
EI_VERIFY(0x005391E0, HasArmourModelsVerify, 6, s_hasArmour);

// 0050E460: MOV EDX,[ESP+8]; MOV ECX,[ECX+0x170] (10 bytes).
static int(__fastcall* s_companionQuery)(CMapObject*, void*, int, int, int);
static int __fastcall CompanionQueryVerify(CMapObject* self, void*, int first, int second, int third) {
    int expected = s_companionQuery(self, nullptr, first, second, third);
    int actual = self->CompanionQuery(first, second, third);
    ei::verify::Check("CMapObject::CompanionQuery", expected, actual, "object %d (%d, %d)", self->id_, first, second);
    return actual;
}
EI_VERIFY(0x0050E460, CompanionQueryVerify, 10, s_companionQuery);

static int __fastcall CreateServerVerify(CMapObjectServer* self) {
    int result = self->CreateServer();
    ei::verify::Check("CMapObjectServer::CreateServer", true, result == 1 && CompanionOwner(self) == nullptr, "");
    return result;
}
EI_VERIFY(0x005157C0, CreateServerVerify, 0, s_unusedCreate);

static void __fastcall DestroyServerVerify(CMapObjectServer* self) {
    self->DestroyServer();
    ei::verify::Check("CMapObjectServer::DestroyServer", true, self->stringCount == 0 && CompanionOwner(self) == nullptr,
                      "%d strings", self->stringCount);
}
EI_VERIFY(0x005157E0, DestroyServerVerify, 0, s_unusedDestroy);

// --- Slots 41 / 44 of map objects, the client companion's Create, the controller's deleting
// destructor (all ours only: they log, free or release). Reached by: slot 41 when a skill is used
// on an object that is no unit (its callers are not traced; the counts show), slot 44 when such
// arrays are freed, the client companion's Create with every object made on the client (units,
// unit_create), the controller's deleting destructor when a client unit is released (unit_create's
// remove, zone changes).
namespace {
void* s_unusedScience;
void* s_unusedFreeArray;
void* s_unusedCompanionCreate;
void* s_unusedDelete;

struct ArrayView {
    void* vtable;
    void* data;
    int size;
    int capacity;
};
} // namespace

static bool __fastcall ApplyScienceVerify(CMapObject* self, void*, int first, int second) {
    bool result = self->CMapObject::ApplyScience(first, second);
    ei::verify::Check("CMapObject::ApplyScience", false, result, "object %d", self->id_);
    return result;
}
EI_VERIFY(0x0050F0F0, ApplyScienceVerify, 0, s_unusedScience);

static void __fastcall FreeArrayVerify(CMapObject* self, void*, void* array, int unused) {
    self->FreeArray(array, unused);
    const auto* view = static_cast<const ArrayView*>(array);
    ei::verify::Check("CMapObject::FreeArray", true, !view->data && view->size == 0 && view->capacity == 0, "object %d",
                      self->id_);
}
EI_VERIFY(0x0050F120, FreeArrayVerify, 0, s_unusedFreeArray);

static int __fastcall CreateClientCompanionVerify(CMapObjectClientSpecific* self) {
    int result = self->CreateClientCompanion();
    ei::verify::Check("CMapObjectClientSpecific::CreateClientCompanion", true,
                      result == 1 && self->endSound == -1 && !self->owner, "result %d", result);
    return result;
}
EI_VERIFY(0x00510770, CreateClientCompanionVerify, 0, s_unusedCompanionCreate);

static CUnitClientSpecific* __fastcall DeleteClientVerify(CUnitClientSpecific* self, void*, std::uint32_t flags) {
    CUnitClientSpecific* result = self->DeleteClient(flags);
    ei::verify::Check("CUnitClientSpecific::DeleteClient", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x00533E80, DeleteClientVerify, 0, s_unusedDelete);
