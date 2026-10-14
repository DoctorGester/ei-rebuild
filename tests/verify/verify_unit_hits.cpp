// Checks for how hits reach the statistics (unit_hits.cpp).
//
// ApplyHit, DamageSlot and the wear functions log, wear items and send party remarks: ours only,
// with the checks that hold for any correct run (the hit points follow the slots; the item's
// condition drops by exactly the wear). UnitVitals::TakeHit changes only the vitals: ours,
// snapshot, restore, original, compare.
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/game/unit.h"

#include <cstring>

#pragma float_control(precise, on)

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// After a slot took the hit the hit points are the slots' reduction of the maximum.
bool HitPointsFollowSlots(UnitStats* stats, UnitVitals* vitals) {
    float expected = static_cast<float>(stats->Reduce(vitals->maxHitPoints));
    return Bits(expected) == Bits(vitals->hitPoints);
}

void* s_unused;
} // namespace

// 005254B0: ours only.
static int __fastcall ApplyHitVerify(UnitStats* self, void*, CUnit* unit, UnitVitals* vitals, const DamageInfo* hit,
                                     CEngineObject* weapon) {
    int result = self->ApplyHit(unit, vitals, hit, weapon);
    ei::verify::Check("UnitStats::ApplyHit", true, result == 0 || HitPointsFollowSlots(self, vitals),
                      "unit %d location %d: hp %g of %g", unit->id_, hit->location, vitals->hitPoints,
                      vitals->maxHitPoints);
    return result;
}
EI_VERIFY(0x005254B0, ApplyHitVerify, 0, s_unused);

// 00525070: ours only.
static int __fastcall DamageSlotVerify(UnitStats* self, void*, CUnit* unit, UnitVitals* vitals, const DamageInfo* hit,
                                       CEngineObject* weapon) {
    bool usable = self->slots[hit->location].kind >= 2;
    int result = self->DamageSlot(unit, vitals, hit, weapon);
    ei::verify::Check("UnitStats::DamageSlot", true, !usable || HitPointsFollowSlots(self, vitals),
                      "unit %d location %d", unit->id_, hit->location);
    return result;
}
EI_VERIFY(0x00525070, DamageSlotVerify, 0, s_unused);

// 005253E0: ours only; the condition drops by the wear (shared by the pieces).
static void __fastcall WearItemVerify(CEngineObject* item, CUnit* unit, float amount, int broken, int worn) {
    bool wears = item && unit && amount > 0.0f;
    float expected = 0.0f;
    if (wears) {
        float condition;
        std::memcpy(&condition, reinterpret_cast<char*>(item) + 0x20, sizeof(condition));
        int pieces;
        std::memcpy(&pieces, reinterpret_cast<char*>(item) + 0x48, sizeof(pieces));
        expected = pieces > 0 ? static_cast<float>(condition - amount / static_cast<double>(pieces))
                              : static_cast<float>(condition - amount);
    }
    WearItem(item, unit, amount, broken, worn);
    float after = 0.0f;
    if (wears) std::memcpy(&after, reinterpret_cast<char*>(item) + 0x20, sizeof(after));
    ei::verify::Check("WearItem", Bits(expected), Bits(after), "amount %g: %g vs %g", amount, expected, after);
}
EI_VERIFY(0x005253E0, WearItemVerify, 0, s_unused);

// 00525720: ours only (counted).
static void __stdcall WearArmourVerify(CUnit* unit, int location, int layer, float amount) {
    WearArmour(unit, location, layer, amount);
    ei::verify::Check("WearArmour", true, true, "");
}
EI_VERIFY(0x00525720, WearArmourVerify, 0, s_unused);

// 00524FE0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static BOOL(__fastcall* s_takeHit)(UnitVitals*, void*, const DamageInfo*);
static BOOL __fastcall TakeHitVerify(UnitVitals* self, void*, const DamageInfo* hit) {
    UnitVitals saved;
    std::memcpy(&saved, self, sizeof(UnitVitals));
    BOOL actual = self->TakeHit(hit);
    UnitVitals after;
    std::memcpy(&after, self, sizeof(UnitVitals));
    std::memcpy(self, &saved, sizeof(UnitVitals));
    BOOL expected = s_takeHit(self, nullptr, hit);
    bool same = expected == actual && std::memcmp(&after, self, sizeof(UnitVitals)) == 0;
    ei::verify::Check("UnitVitals::TakeHit", true, same, "hp %g vs %g", self->hitPoints, after.hitPoints);
    return expected;
}
EI_VERIFY(0x00524FE0, TakeHitVerify, 6, s_takeHit);

// 00530A60: SUB ESP,0x18; PUSH ESI; MOV ESI,ECX (6 bytes). Pure.
static BOOL(__fastcall* s_isBehind)(CUnit*, void*, const CEngineObject*);
static BOOL __fastcall IsBehindVerify(CUnit* self, void*, const CEngineObject* target) {
    BOOL expected = s_isBehind(self, nullptr, target);
    ei::verify::Check("CUnit::IsBehind", expected, self->IsBehind(target), "unit %d target %d", self->id_, target->id_);
    return expected;
}
EI_VERIFY(0x00530A60, IsBehindVerify, 6, s_isBehind);
