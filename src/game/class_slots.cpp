// Small virtuals of the early classes: the lightning manager, CUnit and CMapObject. See docs/notes/objects.md.
#include "lightning.h"
#include "map_object.h"
#include "object.h"
#include "spell.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstdint>
#include <new>
#include <string>

namespace {

int g_lightningNoiseLoaded = /*007A9694*/ {};

} // namespace

// ---- The lightning manager --------------------------------------------------------------------------

void CLightningManager::Create() {
    if (!g_lightningNoiseLoaded) {
        g_lightningNoiseLoaded = 1;
        LoadLightningNoise();
    }
    CBaseManager::Create();
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

void CUnit::SetScriptName(std::string_view newName) { scriptName = newName; }

// Of the unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light); whatever it returns is passed on.
int CMapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}
