// Small virtual slots of the early classes: the lightning manager, the CUnit slots and the item bases. See docs/notes/objects.md.
#include "game_strings.h"
#include "item.h"
#include "lightning.h"
#include "map_object.h"
#include "object.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"
#include <string>

#include <cstdint>
#include <new>

#undef GetClassName

namespace {

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

// ---- CUnit slots ----------------------------------------------------------------------------

void CUnit::ReceiveHit(DamageInfo* hit, CUnit* attacker, CUnit* caster) {
    Controller()->TakeHit(hit, attacker, caster);
}

// Nothing reads the result.
int CUnit::ForgetObject(CEngineObject* object) {
    Controller()->ObjectDeleted(object);
    return 0;
}

void CUnit::SetScriptName(const char* newName) { scriptName = newName; }

// Of the unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light); whatever it returns is passed on.
int CMapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}

// ---- CItem's base names and use ----------------------------------------------------------------

// The names a plain item does not have (three identical copies): an empty text.
std::string CItem::ModelName() { return std::string(game::kEmptyText); }
std::string CItem::FigureName() { return std::string(game::kEmptyText); }
std::string CItem::PictureName() { return std::string(game::kEmptyText); }

// Which every class but the quest item keeps: no key.
LockKey CItem::GetUse() { return LockKey{0, 0, 0}; }
