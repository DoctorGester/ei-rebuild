// Checks for unit_equipment.cpp: CMapObjectManager::GiveStartingEquipment (005141F0) makes items
// and spells, so only ours runs. Afterwards the result is 1 exactly when the prototype is known
// (looked up again; a known name is not inserted twice); the unit's spell slots hold min(8, before
// + the prototype's spell names); a unit of type 0x32 holds min(4, before + its named weapons)
// weapons, no fewer armour pieces than before, and when the first weapon is named and there was a
// free slot, +0x270 names a filled weapon slot. A piece that cannot be made (a FatalError message)
// shows as a mismatch too. Reached for every unit the world adds from a zone file (newgame).
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_create.h"
#include "../../src/game/unit_db.h"

#include <algorithm>
#include <cstring>

namespace {
void* s_unused;

int Filled(CEngineObject* const* slots, int count) {
    int filled = 0;
    for (int i = 0; i < count; ++i)
        if (slots[i]) ++filled;
    return filled;
}

bool Named(const mfc::CString& name) {
    return name.GetLength() != 0 && std::strcmp(name, reinterpret_cast<const char*>(0x007846F0)) != 0;   // "none"
}

int Count(const CStringVector& names) { return static_cast<int>(names.end - names.begin); }
} // namespace

static int __fastcall GiveStartingEquipmentVerify(CMapObjectManager* self, void*, CUnit* unit, const char* name) {
    int spellsBefore = Filled(unit->spells, 8);
    int weaponsBefore = Filled(unit->weapons, 4);
    int armourBefore = Filled(unit->armor, 7);
    int result = self->GiveStartingEquipment(unit, name);

    const char* problem = nullptr;
    auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unitdb::PrototypeTable().FindByName(name));
    int spells = Filled(unit->spells, 8);
    int weapons = Filled(unit->weapons, 4);
    if (result != (prototype ? 1 : 0)) {
        problem = "result";
    } else if (prototype) {
        if (spells != std::min(8, spellsBefore + Count(prototype->spellNames))) problem = "spells";
        if (!problem && unit->type_ == 0x32) {
            int named = (Named(prototype->weaponName) ? 1 : 0) + (Named(prototype->secondWeaponName) ? 1 : 0);
            if (weapons != std::min(4, weaponsBefore + named)) problem = "weapons";
            else if (Filled(unit->armor, 7) < armourBefore) problem = "armour";
            else if (Named(prototype->weaponName) && weaponsBefore < 4 &&
                     (static_cast<unsigned>(unit->pendingWeapon) >= 4 || !unit->weapons[unit->pendingWeapon]))
                problem = "weapon slot (+0x270)";
        }
    }
    ei::verify::Check("CMapObjectManager::GiveStartingEquipment", true, problem == nullptr,
                      "unit %d \"%s\": %s (spells %d -> %d, weapons %d -> %d, result %d)", unit->id_, name,
                      problem ? problem : "", spellsBefore, spells, weaponsBefore, weapons, result);
    return result;
}
EI_VERIFY(0x005141F0, GiveStartingEquipmentVerify, 0, s_unused);
