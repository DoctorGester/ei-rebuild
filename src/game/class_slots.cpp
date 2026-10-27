// Small virtual slots of classes below 0x004C0000 (A-E1): the lightning manager, CFakeUnit, and the
// CUnit slots the fake unit shares. Each is a __fastcall shim on the slot's address (this in ECX), as
// object.cpp's getters are. Main thread. See docs/notes/objects.md ("A-E1: small slots").
#include "game_strings.h"
#include "item.h"
#include "lightning.h"
#include "map_object.h"
#include "object.h"
#include "scene_loader.h"
#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

const char* const kFakeUnitName = "CFakeUnit";

// Set once the lightning noise table has been loaded.
int g_lightningNoiseLoaded = /*007A9694*/ {};

} // namespace

// ---- The lightning manager --------------------------------------------------------------------------

int CLightningManager::Create() {
    if (!g_lightningNoiseLoaded) {
        g_lightningNoiseLoaded = 1;
        LoadLightningNoise();
    }
    return CBaseManager::Create();
}
EI_REWRITTEN(0x004689D0, CLightningManager::Create);

// ---- CFakeUnit (vtable 0073BFE8) ------------------------------------------------------------------

// 00446AF0, slot 2 (Create): the skin created and given the unit, then the unit's own Create.
int scene::FakeUnitScene::Create() {
    skin.Create();
    skin.SetOwner(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
    return CreateUnit();
}
int __fastcall FakeUnitCreate(scene::FakeUnitScene* self) { return self->scene::FakeUnitScene::Create(); }
EI_REWRITTEN(0x00446AF0, FakeUnitCreate);

// 004468D0, slot 11.
const char* scene::FakeUnitScene::GetClassName() { return kFakeUnitName; }
const char* __fastcall FakeUnitGetClassName(scene::FakeUnitScene* self) { return self->scene::FakeUnitScene::GetClassName(); }
EI_REWRITTEN(0x004468D0, FakeUnitGetClassName);

// ---- CUnit slots shared with the fake unit (vtables 0073EA8C and 0073BFE8) ------------------------

// 004467F0, slot 31 (RET 0xC): the hit passed to the controller's slot 20 (the server's TakeHit).
void CUnit::ReceiveHit(DamageInfo* hit, CEngineObject* attacker, CEngineObject* caster) {
    Controller()->TakeHit(hit, static_cast<CUnit*>(attacker), static_cast<CUnit*>(caster));
}
EI_REWRITTEN(0x004467F0, CUnit::ReceiveHit);

// 00446850, slot 56 (RET 4): the controller's slot 42 (ObjectDeleted). Nothing reads the result.
int CUnit::ForgetObject(CEngineObject* object) {
    Controller()->ObjectDeleted(object);
    return 0;
}
EI_REWRITTEN(0x00446850, CUnit::ForgetObject);

// 004468B0, slot 59: the forward one of the unit's movement speeds (GetMoveSpeeds).
float __fastcall UnitForwardSpeed(CUnit* unit) {
    float forward;
    float backward;
    unit->GetMoveSpeeds(&forward, &backward);
    return forward;
}
EI_REWRITTEN(0x004468B0, UnitForwardSpeed);

// 00446830, slot 61 (RET 4): the script name (the CString at +0x238) becomes `name`.
void CUnit::SetScriptName(const char* newName) { scriptName = newName; }
EI_REWRITTEN(0x00446830, CUnit::SetScriptName);

// 00446810, slot 58: a tail jump to the controller's slot 31 (RefreshWounds: 1 from the client's,
// 0 from the base's).
int __fastcall UnitRefreshWounds(CUnit* unit) { return unit->Controller()->RefreshWounds(); }
EI_REWRITTEN(0x00446810, UnitRefreshWounds);

// 00446870, slot 57: a tail jump to the controller's slot 53 (the server's recomputes the move
// factors); what that leaves in EAX is passed on.
std::uintptr_t __fastcall UnitMoveFactorsChanged(CUnit* unit) {
    unit->Controller()->MoveFactorsChanged();
    return 0;
}
EI_REWRITTEN(0x00446870, UnitMoveFactorsChanged);

// 004467C0, slot 47 (RET 4) of the unit, the fake unit and nine other map object classes (eleven
// vtables): the visibility mode is handed to the companion's slot 19 (the client's shows or hides
// the object's light); what that leaves in EAX is passed on.
int CMapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}
EI_REWRITTEN(0x004467C0, CMapObject::SetVisibilityMode);

// ---- CItem's base bodies of slots 17-21 (vtables 00743DA4 and 0073C474) ---------------------------

// 00462300 / 00462330 / 00462360 / 00462390 (RET 4; four identical copies), slots 17-20: the names
// a plain item does not have. An empty text built in the caller's result.
mfc::CString CItem::ModelName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::FirstListName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::FigureName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::PictureName() { return mfc::CString(game::EmptyText()); }
EI_REWRITTEN(0x00462300, CItem::ModelName);
EI_REWRITTEN(0x00462330, CItem::FirstListName);
EI_REWRITTEN(0x00462360, CItem::FigureName);
EI_REWRITTEN(0x00462390, CItem::PictureName);

// 004623C0 (RET 4: the caller's result), slot 21, which every class but the quest item keeps: no key.
LockKey CItem::GetUse() { return LockKey{0, 0, 0}; }
EI_REWRITTEN(0x004623C0, CItem::GetUse);
