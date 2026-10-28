// Small virtual slots of the early classes: the lightning manager, CFakeUnit, and the CUnit slots the fake unit
// shares. See docs/notes/objects.md.
#include "game_strings.h"
#include "item.h"
#include "lightning.h"
#include "map_object.h"
#include "object.h"
#include "scene_loader.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>
#include <new>

#undef GetClassName

namespace {

const char* const kFakeUnitName = "CFakeUnit";

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

// ---- CFakeUnit --------------------------------------------------------------------------------

// (Create): the skin created and given the unit, then the unit's own Create.
int scene::FakeUnitScene::Create() {
    skin.Create();
    skin.SetOwner(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
    return CreateUnit();
}

const char* scene::FakeUnitScene::GetClassName() { return kFakeUnitName; }

// ---- CUnit slots shared with the fake unit ----------------------------------------------------

void CUnit::ReceiveHit(DamageInfo* hit, CUnit* attacker, CUnit* caster) {
    Controller()->TakeHit(hit, attacker, caster);
}

// Nothing reads the result.
int CUnit::ForgetObject(CEngineObject* object) {
    Controller()->ObjectDeleted(object);
    return 0;
}

void CUnit::SetScriptName(const char* newName) { scriptName = newName; }

// Of the unit, the fake unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light); whatever it returns is passed on.
int CMapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}

// ---- CItem's base names and use ----------------------------------------------------------------

// The names a plain item does not have (four identical copies): an empty text.
mfc::CString CItem::ModelName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::FirstListName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::FigureName() { return mfc::CString(game::EmptyText()); }
mfc::CString CItem::PictureName() { return mfc::CString(game::EmptyText()); }

// Which every class but the quest item keeps: no key.
LockKey CItem::GetUse() { return LockKey{0, 0, 0}; }
