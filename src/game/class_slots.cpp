// Small virtual slots of classes below 0x004C0000 (A-E1): CHoover, CLightning, its segments and its
// manager, CFakeUnit, and the CUnit slots the fake unit shares. Each is a __fastcall shim on the
// slot's address (this in ECX), as object.cpp's getters are: the classes' own virtual interfaces
// are not declared in C++. Main thread. See docs/notes/objects.md ("A-E1: small slots").
#include "game_strings.h"
#include "item.h"
#include "lightning.h"
#include "map_object.h"
#include "object.h"
#include "originals_engine.h"
#include "scene_loader.h"
#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <new>

namespace {

const char* const kHooverName = game::Text(0x00785784);      // "CHoover"
const char* const kLightningName = game::Text(0x00787CF0);   // "CLightning"
const char* const kFakeUnitName = game::Text(0x007842F0);    // "CFakeUnit"

constexpr int kClassLightning = 0x1E;
constexpr int kClassHoover = 0x1F;

// 007A9694: set once the lightning noise table has been loaded (00468A00).
int& LightningNoiseLoaded() { return *reinterpret_cast<int*>(0x007A9694); }

} // namespace

// ---- CHoover (vtable 0073C434) --------------------------------------------------------------------

// 00461C60, slot 0 (RET 4): the destructor body, then operator delete when bit 0 of `flags` is set.
CHoover* __fastcall HooverDeletingDestruct(CHoover* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00461C60, HooverDeletingDestruct);

// 00461C90, slot 2 (Create): 1.
int __fastcall HooverCreate(CHoover*) { return 1; }
EI_REPLACE(0x00461C90, HooverCreate);

// 00461C40 / 00461C50, slots 11 / 12.
const char* __fastcall HooverGetClassName(const CHoover*) { return kHooverName; }
EI_REPLACE(0x00461C40, HooverGetClassName);
int __fastcall HooverGetClassId(const CHoover*) { return kClassHoover; }
EI_REPLACE(0x00461C50, HooverGetClassId);

// ---- CLightning (vtable 0073C550) and its manager (0073F5B8) --------------------------------------

// 004687F0, slot 0 (RET 4).
CLightning* __fastcall LightningDeletingDestruct(CLightning* self, void*, unsigned flags) {
    LightningDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004687F0, LightningDeletingDestruct);

// 004687D0 / 004687E0, slots 11 / 12.
const char* __fastcall LightningGetClassName(const CLightning*) { return kLightningName; }
EI_REPLACE(0x004687D0, LightningGetClassName);
int __fastcall LightningGetClassId(const CLightning*) { return kClassLightning; }
EI_REPLACE(0x004687E0, LightningGetClassId);

void LightningSegment::Destruct() {
    if (points.data) LightningPointArray::Free(points.data);
}
EI_REPLACE_METHOD(0x00468920, LightningSegment::Destruct);

int CLightningManager::Create() {
    if (!LightningNoiseLoaded()) {
        LightningNoiseLoaded() = 1;
        LoadLightningNoiseOriginal();
    }
    return CBaseManager::Create();
}
namespace {
int __fastcall LightningManagerCreateHook(CLightningManager* self) { return self->CLightningManager::Create(); }
} // namespace
EI_REPLACE(0x004689D0, LightningManagerCreateHook);

// ---- CFakeUnit (vtable 0073BFE8) ------------------------------------------------------------------

// 00446AF0, slot 2 (Create): the skin created and given the unit, then the unit's own Create.
int __fastcall FakeUnitCreate(scene::FakeUnitScene* self) {
    self->skin.Create();
    self->skin.SetOwner(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self)));
    return reinterpret_cast<CUnit*>(self)->CreateUnit();
}
EI_REPLACE(0x00446AF0, FakeUnitCreate);

// 004468D0, slot 11.
const char* __fastcall FakeUnitGetClassName(const CUnit*) { return kFakeUnitName; }
EI_REPLACE(0x004468D0, FakeUnitGetClassName);

// ---- CUnit slots shared with the fake unit (vtables 0073EA8C and 0073BFE8) ------------------------

// 004467F0, slot 31 (RET 0xC): the hit passed to the controller's slot 20 (the server's TakeHit).
int __fastcall UnitTakeHit(CUnit* unit, void*, DamageInfo* hit, CEngineObject* attacker, CEngineObject* source) {
    return ServerSlots(unit->controller)->TakeHitSlot(hit, attacker, source);
}
EI_REPLACE(0x004467F0, UnitTakeHit);

// 00446850, slot 56 (RET 4): the controller's slot 42 (ObjectDeleted).
void __fastcall UnitObjectDeleted(CUnit* unit, void*, CEngineObject* object) {
    unit->Controller()->ObjectDeleted(object);
}
EI_REPLACE(0x00446850, UnitObjectDeleted);

// 004468B0, slot 59: the forward one of the unit's movement speeds (GetMoveSpeeds).
float __fastcall UnitForwardSpeed(CUnit* unit) {
    float forward;
    float backward;
    unit->GetMoveSpeeds(&forward, &backward);
    return forward;
}
EI_REPLACE(0x004468B0, UnitForwardSpeed);

// 00446830, slot 61 (RET 4): the script name (the CString at +0x238) becomes `name`. Returns the
// string, as the assignment does.
const mfc::CString* __fastcall UnitSetScriptName(CUnit* unit, void*, const char* name) {
    mfc::CString* text = &unit->scriptName;
    return &(*text = name);
}
EI_REPLACE(0x00446830, UnitSetScriptName);

// 00446810, slot 58: a tail jump to the controller's slot 31 (RefreshWounds: 1 from the client's,
// 0 from the base's).
int __fastcall UnitRefreshWounds(CUnit* unit) { return unit->Controller()->RefreshWounds(); }
EI_REPLACE(0x00446810, UnitRefreshWounds);

// 00446870, slot 57: a tail jump to the controller's slot 53 (the server's recomputes the move
// factors); what that leaves in EAX is passed on.
std::uintptr_t __fastcall UnitMoveFactorsChanged(CUnit* unit) { return unit->Controller()->MoveFactorsChanged(); }
EI_REPLACE(0x00446870, UnitMoveFactorsChanged);

// 004467C0, slot 47 (RET 4) of the unit, the fake unit and nine other map object classes (eleven
// vtables): the visibility mode is handed to the companion's slot 19 (the client's shows or hides
// the object's light); what that leaves in EAX is passed on.
std::uintptr_t __fastcall MapObjectSetVisibilityMode(CMapObject* object, void*, int mode) {
    return static_cast<CMapObjectCompanion*>(object->companion)->SetLightMode(mode);
}
EI_REPLACE(0x004467C0, MapObjectSetVisibilityMode);

// ---- CItem's base bodies of slots 17-21 (vtables 00743DA4 and 0073C474) ---------------------------

// 00462300 / 00462330 / 00462360 / 00462390 (RET 4; four identical copies), slots 17-20: the names
// a plain item does not have. An empty text built in the caller's result, which is returned.
mfc::CString* __fastcall ItemNoName(CItem*, void*, mfc::CString* out) {
    return new (out) mfc::CString(game::EmptyText());
}
EI_REPLACE(0x00462300, ItemNoName);
EI_REPLACE(0x00462330, ItemNoName);
EI_REPLACE(0x00462360, ItemNoName);
EI_REPLACE(0x00462390, ItemNoName);

// 004623C0 (RET 4), slot 21, which every class but the quest item keeps: the caller's result
// zeroed and returned. (Slot 16, 004622F0, is four bytes and cannot be hooked.)
ItemUse* __fastcall ItemNoUse(CItem*, void*, ItemUse* out) {
    out->kind = 0;
    out->value = 0;
    out->extra = 0;
    return out;
}
EI_REPLACE(0x004623C0, ItemNoUse);
