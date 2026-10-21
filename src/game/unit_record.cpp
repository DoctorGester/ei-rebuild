// The unit record applied to a new unit (00477B00). See unit_record.h.
#include "item.h"
#include "unit.h"
#include "unit_db.h"
#include "unit_record.h"

#include "combat.h"
#include "ei/hook.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/nan_audit.h"

namespace unitrecord {
namespace {

// The item and spell factory (global pointer at 007AFFB4) and the spell manager the spells are made for
// (007AFFAC). The record names its items by their names.
CItem* CreateItem(CItemManager* factory, const char* name) {   // 006AC970 (patched by Rebirth)
    return ItemManagerCreateItemOriginal(factory, nullptr, name);
}
CItemManager* TheFactory() { return ei::GameData<CItemManager*>(0x007AFFB4); }
CSpellManager* SpellOwner() { return ei::GameData<CSpellManager*>(0x007AFFAC); }

void AddItems(const UnitRecord& record, CUnit* unit, CItemManager* factory) {
    const mfc::CStringArray& quick = record.lists[UnitRecord::kQuickItems];
    for (int i = 0; i < quick.size; ++i) unit->AddQuickItem(CreateItem(factory, quick.data[i]));
    const mfc::CStringArray& carried = record.lists[UnitRecord::kCarried];
    for (int i = 0; i < carried.size; ++i) unit->AddCarried(CreateItem(factory, carried.data[i]));
}

} // namespace

void UnitRecord::ApplyTo(CUnit* target) {
    const UnitRecordStats& r = stats;
    CItemManager* factory = TheFactory();
    CUnit* unit = target;
    const float k0 = ei::GameData<float>(0x0073B858);   // 0.0
    EI_NAN_AUDIT(r.speed, r.rate);
    bool noStats = r.speed == k0 && r.maxHitPoints == 0 && r.rate == k0;
    if (noStats || applyStats == 0) {
        AddItems(*this, unit, factory);
        return;
    }

    target->ReleaseAllItems();
    UnitVitals& vitals = target->ServerCompanion()->vitals;   // +0x170
    UnitStats& s = target->ServerController()->stats;         // controller +0x1E0
    vitals.hitPoints = static_cast<float>(r.hitPoints);
    vitals.maxHitPoints = static_cast<float>(r.maxHitPoints);
    vitals.mana = static_cast<float>(r.mana);
    vitals.maxMana = static_cast<float>(r.maxMana);
    s.SetSlotMaxima(vitals.maxHitPoints);   // 00525A00
    s.RefillSlots();                        // 00525AF0
    s.fixedMaxHitPoints = vitals.maxHitPoints;
    s.speed = r.speed;
    s.rate = r.rate;
    for (int i = 0; i < 4; ++i) s.modeSpeeds[i] = r.modeSpeeds[i];
    s.sightCone = r.sightCone;
    s.peripheralRange = r.peripheralRange;
    s.sideCone = r.sideCone;
    s.modelField8 = r.modelField8;
    s.sizeClass = r.sizeClass;
    s.sizeClass20 = r.sizeClassSmall;
    s.sizeClass60 = r.sizeClassSmall;
    s.combat[0] = r.reach;
    s.combat[1] = r.attack;
    s.combat[3] = r.defence;
    s.combat[4] = r.combatSpeed;
    s.damage = r.damage;
    s.damageSpread = r.damageSpread;
    libcrt::Memmove(s.damageWeights, r.damageWeights, sizeof r.damageWeights);

    // Seven model values scaled by the record's byte, each stored twice.
    for (std::size_t k = 0; k < 7; ++k) {
        const UnitModelRecord* model = target->GetPrototypeModel();
        const float scaled = static_cast<float>(static_cast<double>(r.protectionScale) * model->protection[k]);
        vitals.protection[k].base = scaled;
        vitals.protection[k].current = scaled;
    }
    for (std::size_t i = 0; i < 6; ++i) s.valuesB0[i].base = s.valuesB0[i].current = r.valuesB0[i];
    for (std::size_t i = 0; i < 6; ++i) s.valuesE0[i].base = s.valuesE0[i].current = r.valuesE0[i];
    libcrt::Memmove(s.attributes, r.attributes, sizeof r.attributes);

    const mfc::CStringArray& spells = lists[kSpells];
    for (int i = 0; i < spells.size; ++i) unit->AddSpell(factory->CreateSpellItem(spells.data[i], SpellOwner()));
    AddItems(*this, unit, factory);
    const mfc::CStringArray& armour = lists[kArmour];
    for (int i = 0; i < armour.size; ++i) unit->EquipArmour(CreateItem(factory, armour.data[i]));
    const mfc::CStringArray& weapons = lists[kWeapons];
    for (int i = 0; i < weapons.size; ++i) unit->AddWeapon(CreateItem(factory, weapons.data[i]));
}
EI_REWRITTEN(0x00477B00, UnitRecord::ApplyTo);

} // namespace unitrecord
