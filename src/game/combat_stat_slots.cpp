// CUnitServer's statistics slots: the vitals and the protection recomputed, the statistics repacked, the
// armour layers, the equipment's AI options, experience. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "ui.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/x87.h"
#include "logic_settings.h"

#include <cmath>
#include <cstring>

namespace {
// The defence factor of units without a player, per difficulty (options::g_options->difficulty_).
inline const float* DefenceFactors() { return g_logicSettings.defence; }

UnitVitals& VitalsOf(CUnit* unit) { return unit->ServerCompanion()->vitals; }

AIBrain* BrainOf(CUnitServer* self) { return &self->brain; }

} // namespace

void CUnitServer::RefreshVitals(bool withHitPoints) {
    int now = WorldContext()->tick_;
    float oldRate = stats.rate;
    if (withHitPoints) stats.RecomputeHitPoints(&VitalsOf(unit), unit);
    stats.Recompute(&VitalsOf(unit), unit);
    if (nextAttackTime > now) {
        float scaled = static_cast<float>(static_cast<double>(nextAttackTime - now) * oldRate / stats.rate);
        nextAttackTime = x87::RoundToInt(scaled) + now;
    }
    unit->MarkChanged(0);
}

void CUnitServer::UpdateProtection() {
    stats.PackBodyProtection(unit, &VitalsOf(unit), &protection);
    float factor = unit->player ? 1.0f : DefenceFactors()[options::g_options->difficulty_];
    CItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
    if (unit->player && weapon) {
        double bonus = stats.WeaponSkillBonus(weapon->GetType(), unit);
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(bonus * factor));
    } else {
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(static_cast<double>(factor) * stats.combat[UnitStats::kDefence]));
    }
}

void CUnitServer::RepackStats() {
    std::uint8_t speedsBefore[sizeof(unit->params.modeSpeeds)];
    std::memcpy(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore));
    stats.Pack(&unit->params);
    stats.flags = 0;
    UpdateProtection();
    unit->MarkChanged(4);
    if (std::memcmp(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore)) != 0) motionChanged = 1;
}

bool CUnitServer::ArmourPutOn(std::uint32_t key) {
    // A key out of range gives no record; the original then faults reading its type, as this does.
    auto* record = itemdb::ArmorAt(key >> 16);
    stats.AddArmourLayer(unit->armor[record->type]);
    return false;
}

bool CUnitServer::RebuildArmourLayers() {
    stats.RebuildArmourLayers(unit);
    return true;
}

int CUnitServer::ClearOuterArmourLayers() { return stats.ClearOuterLayers(); }

void CUnitServer::RefreshEquipment() {
    RefreshItemSpells();
    BrainOf(this)->attention.Build(unit);
}

void CUnitServer::HostileMaskChanged() {
    claimedHostile = 0;
    BrainOf(this)->memory.calls.RemoveClaims();
}

void CUnitServer::CopyStats(UnitStats* out) { std::memcpy(out, &stats, sizeof(UnitStats)); }

void CUnitServer::AddExperience(float amount) {
    stats.GainExperience(unit, amount);
    RefreshVitals(false);
}
