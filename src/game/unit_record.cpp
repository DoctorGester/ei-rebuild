// The unit record applied to a new unit. See unit_record.h.
#include "item.h"
#include "item_manager.h"
#include "unit.h"
#include "unit_db.h"
#include "unit_record.h"

#include "combat.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "world.h"

#include <cstring>

namespace unitrecord {
namespace {

// The item and spell factory (the server world's item manager) and the spell manager the spells are made for. The
// record names its items by their names.
Item* CreateItem(ItemManager* factory, std::string_view name) {   // patched by Rebirth
    return factory->CreateItem(name);
}

void AddItems(const UnitRecord& record, CUnit* unit, ItemManager* factory) {
    for (const std::string& name : record.lists[UnitRecord::kQuickItems])
        unit->AddQuickItem(CreateItem(factory, name));
    for (const std::string& name : record.lists[UnitRecord::kCarried]) unit->AddCarried(CreateItem(factory, name));
}

} // namespace

void UnitRecord::ApplyTo(CUnit* target) {
    const UnitRecordStats& r = stats;
    ItemManager* factory = g_serverWorld.itemManager;
    CUnit* unit = target;
    const float k0 = 0.0f;
    EI_NAN_AUDIT(r.speed, r.rate);
    bool noStats = r.speed == k0 && r.maxHitPoints == 0 && r.rate == k0;
    if (noStats || applyStats == 0) {
        AddItems(*this, unit, factory);
        return;
    }

    target->ReleaseAllItems();
    UnitVitals& vitals = target->ServerCompanion()->vitals;
    UnitStats& s = target->ServerController()->stats;
    vitals.hitPoints = static_cast<float>(r.hitPoints);
    vitals.maxHitPoints = static_cast<float>(r.maxHitPoints);
    vitals.mana = static_cast<float>(r.mana);
    vitals.maxMana = static_cast<float>(r.maxMana);
    s.SetSlotMaxima(vitals.maxHitPoints);
    s.RefillSlots();
    s.fixedMaxHitPoints = vitals.maxHitPoints;
    s.speed = r.speed;
    s.rate = r.rate;
    for (int i = 0; i < 4; ++i) s.modeSpeeds[i] = r.modeSpeeds[i];
    s.sightCone = r.sightCone;
    s.peripheralRange = r.peripheralRange;
    s.sideCone = r.sideCone;
    s.reachHeightScale = r.reachHeightScale;
    s.sizeClass = r.sizeClass;
    s.sizeClass20 = r.sizeClassSmall;
    s.sizeClass60 = r.sizeClassSmall;
    s.combat[0] = r.reach;
    s.combat[1] = r.attack;
    s.combat[3] = r.defence;
    s.combat[4] = r.combatSpeed;
    s.damage = r.damage;
    s.damageSpread = r.damageSpread;
    std::memmove(s.damageWeights, r.damageWeights, sizeof r.damageWeights);

    // Seven model values scaled by the record's byte, each stored twice.
    for (std::size_t k = 0; k < 7; ++k) {
        const UnitModelRecord* model = target->GetPrototypeModel();
        const float scaled = static_cast<float>(static_cast<double>(r.protectionScale) * model->protection[k]);
        vitals.protection[k].base = scaled;
        vitals.protection[k].current = scaled;
    }
    for (std::size_t i = 0; i < 6; ++i) s.perception[i].base = s.perception[i].current = r.perception[i];
    for (std::size_t i = 0; i < 6; ++i) s.conspicuity[i].base = s.conspicuity[i].current = r.conspicuity[i];
    std::memmove(s.attributes, r.attributes, sizeof r.attributes);

    for (const std::string& name : lists[kSpells]) unit->AddSpell(factory->CreateSpellItem(name, g_serverWorld.spellManager));
    AddItems(*this, unit, factory);
    for (const std::string& name : lists[kArmour]) unit->EquipArmour(CreateItem(factory, name));
    for (const std::string& name : lists[kWeapons]) unit->AddWeapon(CreateItem(factory, name));
}

} // namespace unitrecord
