// A new unit's starting equipment from its prototype (005141F0; the world adds zone units with it
// after ApplyPrototype). See unit_create.h (CMapObjectManager) and docs/notes/units.md ("Sixth
// round").
#include "unit_create.h"

#include "engine_object.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_gameplay.h"

#include <cstdint>

namespace {
constexpr int kTypeArmed = 0x32;            // CEngineObject::type_ of the units given armour and weapons
constexpr int kManagerWorld = 0xAC;         // the manager's map object context (the world)
constexpr int kWorldItemManager = 0x48 / 4; // world slot 18: the item manager
constexpr int kWorldSpellOwner = 0x58 / 4;  // world slot 22: passed on to the spell factory
constexpr std::uintptr_t kTable = 0x007AF358;

// The game's texts: "none" (a weapon name that gives no weapon) and the FatalError formats.
constexpr std::uintptr_t kNone = 0x007846F0;
constexpr std::uintptr_t kNoPrototype = 0x0078F330;   // Can't find prototype "%s".
constexpr std::uintptr_t kNoArmour = 0x0078F308;      // Can't create armor "%s" for unit "%s".
constexpr std::uintptr_t kNoWeapon = 0x0078F2DC;      // Can't create weapon "%s" for unit "%s".
constexpr std::uintptr_t kNoSpell = 0x0078F2B4;       // Can't create spell "%s" for unit "%s".
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// Through their game addresses (rewritten elsewhere; their verify wrappers stay in the path, and
// Rebirth's patches of 006AC970 and 00480080 still run): 005144D0 the prototype table's name
// lookup (UnitPrototypeTable::FindByName, unit_create.cpp; inserts unknown names); 006AC970 the
// item manager's item by name (item_constructor.cpp); 00480080 FatalError (app.cpp); CUnit's
// EquipArmour / AddWeapon / AddSpell (unit_items.cpp, unit.cpp). 006AD1E0, the item manager's
// spell by name (with world slot 22), is not rewritten.
__declspec(naked) UnitPrototypeRecord* __fastcall FindPrototypeAt(void*, void*, const char*) EI_ORIGINAL(0x005144D0)
__declspec(naked) CEngineObject* __fastcall CreateItemAt(void*, void*, const char*) EI_ORIGINAL(0x006AC970)
CEngineObject* __fastcall CreateSpellAt(void* a0, void*, const char* a2, void* a3) { return (CEngineObject*)FactoryCreateSpellOriginal(a0, nullptr, a2, a3); }
CEngineObject* __fastcall EquipArmourAt(CUnit* a0, void*, CEngineObject* a2) { return (CEngineObject*)UnitEquipArmourOriginal((void*)a0, nullptr, (void*)a2); }
int __fastcall AddWeaponAt(CUnit* a0, void*, CEngineObject* a2) { return UnitAddWeaponOriginal((void*)a0, nullptr, (void*)a2); }
__declspec(naked) int __fastcall AddSpellAt(CUnit*, void*, CEngineObject*) EI_ORIGINAL(0x00531820)

std::uint32_t Count(const CStringVector& names) { return static_cast<std::uint32_t>(names.end - names.begin); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const mfc::CString& name) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                           reinterpret_cast<const unsigned char*>(Text(kNone))) != 0 &&
           name.GetLength() != 0;
}

// The weapon named `name`; a FatalError message when it cannot be made (null is returned then).
CEngineObject* MakeWeapon(void* items, const mfc::CString& name, const char* prototype) {
    CEngineObject* weapon = CreateItemAt(items, nullptr, name);
    if (!weapon) FatalErrorOriginal(Text(kNoWeapon), static_cast<const char*>(name), prototype);
    return weapon;
}
} // namespace

int CMapObjectManager::GiveStartingEquipment(CUnit* unit, const char* name) {
    CEngineObject* world = *reinterpret_cast<CEngineObject**>(reinterpret_cast<char*>(this) + kManagerWorld);
    void* spellOwner = ei::VCall<void*>(world, kWorldSpellOwner);
    void* items = ei::VCall<void*>(world, kWorldItemManager);
    UnitPrototypeRecord* prototype = FindPrototypeAt(reinterpret_cast<void*>(kTable), nullptr, name);
    if (!prototype) {
        FatalErrorOriginal(Text(kNoPrototype), name);
        return 0;
    }
    if (unit->type_ == kTypeArmed) {
        // The loop bounds are read again on every pass, as in the original.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            CEngineObject* armour = CreateItemAt(items, nullptr, prototype->armourNames.begin[i]);
            if (armour) EquipArmourAt(unit, nullptr, armour);
            else FatalErrorOriginal(Text(kNoArmour), static_cast<const char*>(prototype->armourNames.begin[i]), name);
        }
        if (Named(prototype->weaponName)) {
            // Added even when it could not be made: AddWeapon(null) returns 0, which becomes the slot.
            int slot = AddWeaponAt(unit, nullptr, MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->unknown_270 = slot;
        }
        if (Named(prototype->secondWeaponName))
            AddWeaponAt(unit, nullptr, MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        CEngineObject* spell = CreateSpellAt(items, nullptr, prototype->spellNames.begin[i], spellOwner);
        if (spell) AddSpellAt(unit, nullptr, spell);
        else FatalErrorOriginal(Text(kNoSpell), static_cast<const char*>(prototype->spellNames.begin[i]), name);
    }
    return 1;
}
EI_REPLACE_METHOD(0x005141F0, CMapObjectManager::GiveStartingEquipment);
