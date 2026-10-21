// CUnitServer's statistics slots (A-U5): the vitals and the protection recomputed, the
// statistics repacked, the armour layers, the equipment's AI options, experience. See combat.h
// and docs/notes/combat.md.
#include "combat.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "originals_ai.h"
#include "ui.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "game_data.h"

#include <cmath>
#include <cstring>

namespace {
// 007C54B8: the defence factor of units without a player, per difficulty (GameDifficulty()).
inline const float* DefenceFactors() { return ei::GamePointer<const float>(0x007C54B8); }

UnitVitals& VitalsOf(CUnit* unit) { return static_cast<CMapObjectServer*>(unit->MapObject()->companion)->vitals; }

AIBrain* BrainOf(CUnitServer* self) { return &self->brain; }

} // namespace

BOOL CUnitServer::RefreshVitals(char withHitPoints) {
    int now = static_cast<CWorld*>(ManagerContext())->tick_;
    float oldRate = Combat().rate;
    if (withHitPoints) stats.RecomputeHitPoints(&VitalsOf(unit), unit);
    stats.Recompute(&VitalsOf(unit), unit);
    if (nextAttackTime > now) {
        float scaled = static_cast<float>(static_cast<double>(nextAttackTime - now) * oldRate / Combat().rate);
        nextAttackTime = x87::RoundToInt(scaled) + now;
    }
    unit->MarkChanged(0);
    return 1;
}
EI_REPLACE_METHOD(0x005533B0, CUnitServer::RefreshVitals);

void CUnitServer::UpdateProtection() {
    stats.PackBodyProtection(unit, &VitalsOf(unit), protection);
    float factor = unit->player ? 1.0f : DefenceFactors()[GameDifficulty()];
    auto* weapon = static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon));
    if (unit->player && weapon) {
        double bonus = stats.WeaponSkillBonus(weapon->GetType(), unit);
        protection[0] = static_cast<std::int16_t>(static_cast<int>(bonus * factor));
    } else {
        protection[0] = static_cast<std::int16_t>(static_cast<int>(static_cast<double>(factor) * Combat().defence));
    }
}
EI_REPLACE_METHOD(0x00553450, CUnitServer::UpdateProtection);

void CUnitServer::RepackStats() {
    std::uint8_t speedsBefore[sizeof(unit->modeSpeeds)];
    std::memcpy(speedsBefore, unit->modeSpeeds, sizeof(speedsBefore));
    stats.Pack(unit, unit->Params());
    stats.flags = 0;
    UpdateProtection();
    unit->MarkChanged(4);
    if (std::memcmp(speedsBefore, unit->modeSpeeds, sizeof(speedsBefore)) != 0) motionChanged = 1;
}
EI_REPLACE_METHOD(0x00553500, CUnitServer::RepackStats);

bool CUnitServer::AddArmourLayerOf(std::uint32_t key) {
    // A key out of range gives no record; the original then faults reading its type, as this does.
    auto* record = reinterpret_cast<const itemdb::ArmorRecord*>(
        itemdb::Armors().RecordAt(key >> 16, itemdb::kArmorSize));
    stats.AddArmourLayer(unit->armor[record->type]);
    return false;
}
EI_REPLACE_METHOD(0x00553560, CUnitServer::AddArmourLayerOf);

bool CUnitServer::RebuildArmourLayers(int) {
    stats.RebuildArmourLayers(unit);
    return true;
}
EI_REPLACE_METHOD(0x005535C0, CUnitServer::RebuildArmourLayers);

int CUnitServer::ClearOuterArmourLayers() { return stats.ClearOuterLayers(); }
EI_REPLACE_METHOD(0x005535E0, CUnitServer::ClearOuterArmourLayers);

BOOL CUnitServer::RefreshEquipment() {
    RefreshItemSpells();
    BrainOf(this)->attention.Build(unit);
    return 1;
}
EI_REPLACE_METHOD(0x005535F0, CUnitServer::RefreshEquipment);

void CUnitServer::ForgetClaims() {
    claimedHostile = 0;
    BrainOf(this)->memory.calls.RemoveClaims();
}
EI_REPLACE_METHOD(0x00552CD0, CUnitServer::ForgetClaims);

void CUnitServer::CopyStats(UnitStats* out) { std::memcpy(out, &stats, sizeof(UnitStats)); }
EI_REPLACE_METHOD(0x00553360, CUnitServer::CopyStats);

BOOL CUnitServer::AddExperience(float amount) {
    stats.GainExperience(unit, amount);
    return RefreshVitals(0);
}
EI_REPLACE_METHOD(0x00553380, CUnitServer::AddExperience);
