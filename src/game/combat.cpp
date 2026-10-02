// Combat: CUnitServer's weapon and spell queries, the attack hit (damage calculation), the attack
// roll and hit resolution. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "item.h"
#include "object_tables.h"
#include "object_diplomacy.h"
#include "random.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

// /fp:fast reassociates and distributes; the formulas here must keep the original's order of
// operations. Values the original keeps on the FPU stack are double locals, values it stores
// (FSTP to memory) are float locals.
#pragma float_control(precise, on)

namespace {
constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kOne = 1.0f;                                        // 0073B84C
constexpr float kPointOne = std::bit_cast<float>(0x3DCCCCCDu);      // 0073B850 (0.1)
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);      // 0073EF28 (0.2)
constexpr float kPointThree = std::bit_cast<float>(0x3E99999Au);    // 0073C54C (0.3)
constexpr float kPointSix = std::bit_cast<float>(0x3F19999Au);      // 0073CDE0 (0.6)
constexpr float kOnePointFive = 1.5f;                               // 0073C51C
constexpr float kFive = 5.0f;                                       // 0073EED4
constexpr float kTen = 10.0f;                                       // 0073BAAC
constexpr float kDistanceScale = 10.0f;                             // 0073EEDC
constexpr float kFifteen = 15.0f;                                   // 0073C518
constexpr float kSpeedScale = 20.0f;                                // 0073EED8
constexpr float kCalledShotHead = 25.0f;                            // 0073E124
constexpr float kHundred = 100.0f;                                  // 0073BAB0
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0073C548 (0.01)
constexpr float kHitMark = std::bit_cast<float>(0x7F7FFFFFu);       // 0073F13C (FLT_MAX)
constexpr float kMissMark = std::bit_cast<float>(0xFF7FFFFFu);      // 0073C1A8 (-FLT_MAX)
// Melee reach floors of ActionDistance (immediates in 0054C1AB / 0054C1C3).
constexpr float kMeleeFloor = std::bit_cast<float>(0x3F19999Au);    // 0.6
constexpr float kMeleeFloorParty = std::bit_cast<float>(0x3E4CCCCEu); // 0.20000002 (sic)

// Size classes (00552FB0) and movement factors (005536B0).
constexpr float kHeroSizeCap = std::bit_cast<float>(0x3FF1EB85u);   // immediate (1.89)
constexpr float kLoadOverloaded = 1.5f;                             // 0073C51C
constexpr float kLoadHeavy = std::bit_cast<float>(0x3F59999Au);     // 0073CEB8 (0.85)
constexpr float kSizeHeavy = std::bit_cast<float>(0x3FD9999Au);     // 0073F138 (1.7)
constexpr float kSizeSmall = kPointSix;                             // 0073CDE0
constexpr float kSizeMedium = std::bit_cast<float>(0x3F99999Au);    // 0073CDF4 (1.2)
constexpr float kSizeLarge = std::bit_cast<float>(0x3FF33333u);     // 0073CF2C (1.9)
constexpr float kSizeScale20 = kPointTwo;                           // 0073BAD4 (0.2)
constexpr float kWorldFactorScale = kPercent;                       // 0073D09C (0.01)
constexpr float kMode0Factor2 = 0.5f;                               // 0073EF04
constexpr float kMode1Factor2 = 0.75f;                              // 0073EF08
constexpr float kMode0Factor3 = 1.0f;                               // 0073EF14
constexpr float kIdleMode0Factor0 = std::bit_cast<float>(0x3ECCCCCDu);  // 0073EF00 (0.4)
constexpr float kIdleMode1Factor0 = kPointTwo;                      // 0073EEFC (0.2)
constexpr float kIdleMode3Scale = 2.0f;                             // 0073EEF4
constexpr float kIdleMode3Factor1 = 0.5f;                           // 0073EF10
constexpr float kCastingFactor2 = 1.5f;                             // 0073EF0C
constexpr float kSpeedFast = kSizeMedium;                           // 0073CDF4 (1.2)
constexpr float kSpeedSlow = std::bit_cast<float>(0x3E19999Au);     // 0073CD18 (0.15)
constexpr int kUnitSlotSpeed = 59;      // CUnit vtable slot 59 (first output of 0052F920)

constexpr int kClassQuickItem = 0x13;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;   // UnitAction::source of a skill used on a target
constexpr int kNoNaturalWeapon = 0x10;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr int kUnitSlotIsDead = 55;     // CUnit vtable slot 55 (+0x274)
constexpr int kUnitSlotRadius = 60;     // CUnit vtable slot 60 (float +0x328)
constexpr int kSlotAttackParams = 50;   // CUnitServer vtable slot 50 (GetAttackParams)
constexpr std::uint32_t kFlagSpellBlocked = 0x10000000;

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
float Global7C5488() { return *reinterpret_cast<const float*>(0x007C5488); }
float Global7C548C() { return *reinterpret_cast<const float*>(0x007C548C); }
// Difficulty (0079BEC0) and the attack factor per difficulty (007C54AC, floats).
float DifficultyAttackFactor() {
    int difficulty = *reinterpret_cast<const int*>(0x0079BEC0);
    return reinterpret_cast<const float*>(0x007C54AC)[difficulty];
}

// The unit's server object (CUnit + 0x170); +0x34 is its mana.
float ServerMana(const CUnit* unit) {
    const char* server = *reinterpret_cast<char* const*>(reinterpret_cast<const char*>(unit) + 0x170);
    return Field<float>(server, 0x34);
}

std::uint8_t PlayerMode108(const char* player) { return Field<std::uint8_t>(player, 0x108); }

// x87 FCOM: "ST < operand or unordered" (C0), as `TEST AH,1` reads it.
bool LessOrUnordered(double a, double b) { return !(a >= b); }

// The original's branch-free max(0, v): FLD 0; FCOMP v; C0 picks v (0 < v, or v is NaN), else +0.
float MaxZero(float v) { return LessOrUnordered(kZero, v) ? v : kZero; }

// A zero-extended dword loaded with FILD qword (exact; a plain uint32 conversion would add 2^32
// with an FADD that precision control rounds).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// FLD/FSTP copy of a float (the original copies these through the FPU).
void FpuCopy(void* dst, const void* src) {
    __asm {
        mov eax, src
        fld dword ptr [eax]
        mov eax, dst
        fstp dword ptr [eax]
    }
}

float FloatAt(const void* p, int offset) {
    float f;
    std::memcpy(&f, static_cast<const char*>(p) + offset, sizeof(f));
    return f;
}

// The game's _ftol (006E853C): truncation; out-of-range values keep the low 32 bits of the
// 64-bit result (unlike the modern CRT's conversion).
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

CItemWeapon* ActiveWeapon(CUnit* unit) { return static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon)); }
} // namespace

// Not rewritten (forwarders). UnitStats and CUnit methods of the unit range, as __fastcall with
// the unused EDX.
// 00523D30. Scales a damage value by two of the unit's effect values (types 0x1D / 0x1E).
__declspec(naked) float __fastcall StatsScaleDamage(UnitStats*, void*, CUnit*, float) EI_ORIGINAL(0x00523D30)
// 00526C70. The skill bonus for weapon type `type`. Returned in ST0 unrounded: declared double.
__declspec(naked) double __fastcall StatsWeaponSkillBonus(UnitStats*, void*, int, CUnit*) EI_ORIGINAL(0x00526C70)
// 005304A0 / 00530560. The unit's load and size (2 * +0x14C), in ST0: declared double.
__declspec(naked) double __fastcall UnitLoad(CUnit*) EI_ORIGINAL(0x005304A0)
__declspec(naked) double __fastcall UnitSize(CUnit*) EI_ORIGINAL(0x00530560)
// 0052E0D0. Scales AttackParams::attack and ::defence by the difficulty unless `keyed`.
__declspec(naked) void __fastcall DifficultyScaleParams(AttackParams*, void*, int) EI_ORIGINAL(0x0052E0D0)
// 00530A60. Is the unit behind `target` (facing test)? Only AL is meaningful.
__declspec(naked) int __fastcall UnitIsBehind(CUnit*, void*, CEngineObject*) EI_ORIGINAL(0x00530A60)
// 005254B0. Applies `hit` (with the attacker's `weapon`, may be null) to the unit's server stats;
// 0 when it did nothing.
__declspec(naked) int __fastcall StatsApplyHit(UnitStats*, void*, CUnit*, void*, DamageInfo*, CEngineObject*)
    EI_ORIGINAL(0x005254B0)
// 00524560. Recomputes the unit's server stats (hit points and the like) after a change.
__declspec(naked) void __fastcall StatsRecompute(UnitStats*, void*, void*, CUnit*) EI_ORIGINAL(0x00524560)
// 005911C0. The world server's experience award for a kill (`player` null in multiplayer).
__declspec(naked) void __fastcall WorldAwardKill(void*, void*, char*, int) EI_ORIGINAL(0x005911C0)
// 005D7E90. The AI's memory of attackers (CUnitServer + 0x980): remembers `attacker`.
__declspec(naked) void __fastcall RememberAttacker(void*, void*, CUnit*) EI_ORIGINAL(0x005D7E90)

__declspec(naked) GameOstream* GameOstream::Put(const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) GameOstream* GameOstream::Put(long) EI_ORIGINAL(0x006FC9C6)
__declspec(naked) GameOstream* GameOstream::Put(double) EI_ORIGINAL(0x006FC7FA)
__declspec(naked) GameOstream* GameOstream::PutChar(int) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) GameOstream* GameOstream::Apply(void*) EI_ORIGINAL(0x00434FE0)

namespace {

// Names of the hit locations (DamageInfo +0x2C) in the log.
const char* HitLocationName(int location) {
    switch (static_cast<std::uint32_t>(location)) {
    case 0: return "Head!";
    case 1: return "Body";
    case 2: return "LHand";
    case 3: return "RHand";
    case 4: return "LLeg";
    case 5: return "RLeg";
    case 6: return "all";
    default: return "xz";
    }
}

// CUnit + 0x350: what last damaged the unit: a weapon type, 0x10 + the attacking creature's
// +0x18 (its prototype index), or 7 (elemental or no attacker).
std::int16_t& LastDamageSource(CUnit* unit) {
    return *reinterpret_cast<std::int16_t*>(reinterpret_cast<char*>(unit) + 0x350);
}
} // namespace

float CUnitServer::AttackRange() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return weapon->unknown_58;
    return Combat().reach;
}
EI_REPLACE_METHOD(0x0054B620, CUnitServer::AttackRange);

int CUnitServer::WeaponType() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return weapon->GetType();
    return Combat().naturalWeapon;
}
EI_REPLACE_METHOD(0x0054B650, CUnitServer::WeaponType);

BOOL CUnitServer::HasRangedWeapon() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (!weapon) return 0;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow ? 1 : 0;
}
EI_REPLACE_METHOD(0x0054B680, CUnitServer::HasRangedWeapon);

BOOL CUnitServer::AttacksFromRange() {
    if (!ActiveWeapon(unit)) {
        return Combat().naturalWeapon != kNoNaturalWeapon && Combat().reach > kOne ? 1 : 0;
    }
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (!weapon) return 0;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow ? 1 : 0;
}
EI_REPLACE_METHOD(0x0054B6C0, CUnitServer::AttacksFromRange);

int CUnitServer::ScaledItemValue(const CItem* item) {
    if (!item) return 0;
    double factor = Combat().unknown_30;    // stays on the FPU stack
    if (factor < kZero || factor > kZero) factor = kFifteen / factor;
    float value = static_cast<float>(Unsigned(Field<std::uint32_t>(reinterpret_cast<const char*>(item), 0x4C)) * factor);
    return x87::fistp(value);
}
EI_REPLACE_METHOD(0x0054B760, CUnitServer::ScaledItemValue);

int CUnitServer::ScaledAttackValue(int kind, CItem* item) {
    float factor = Combat().unknown_30;
    if (factor < kZero || factor > kZero) factor = kFifteen / factor;
    if (kind != 3) return 0;
    int value;
    if (item) value = Field<int>(item->GetPrototype(), 0x6C);
    else value = GameFtol(Combat().unknown_8C);
    float scaled = static_cast<float>(static_cast<double>(value) * factor);
    return x87::fistp(scaled);
}
EI_REPLACE_METHOD(0x0054B9C0, CUnitServer::ScaledAttackValue);

void CUnitServer::GetDamageByType(std::int16_t* out) {
    CUnit* self = unit;
    CItemWeapon* weapon;
    if (self->player && self->activeWeapon < 4 &&
        (weapon = static_cast<CItemWeapon*>(self->weapons[self->activeWeapon])) != nullptr) {
        float total = weapon->damageSpread_ + weapon->damage_;
        float scale = StatsScaleDamage(&stats, nullptr, self, total);
        float weights[7];
        weapon->GetDamageTypes(weights);
        for (int i = 0; i < 7; ++i) {
            float value = scale * weights[i];
            out[1 + i] = static_cast<std::int16_t>(x87::fistp(value));
        }
        return;
    }
    float total = Combat().damageSpread + Combat().damage;
    float scale = StatsScaleDamage(&stats, nullptr, self, total);
    for (int i = 0; i < 7; ++i) {
        float value = scale * Combat().damageWeights[i];
        out[1 + i] = static_cast<std::int16_t>(x87::fistp(value));
    }
}
EI_REPLACE_METHOD(0x0054BA30, CUnitServer::GetDamageByType);

void CUnitServer::GetSpellDamage(std::uint32_t source, std::int16_t* out) {
    void* spell = SpellOfSource(source);
    std::memset(out, 0, 16);
    if (spell) {
        auto value = static_cast<std::int16_t>(GameFtol(FloatAt(spell, 0x24)));
        int type = static_cast<CSpell*>(spell)->GetDamageType();
        out[1 + type] = value;
    }
    out[0] = 0x7FFF;
}
EI_REPLACE_METHOD(0x0054BB20, CUnitServer::GetSpellDamage);

CItem* CUnitServer::SpellCarrier(std::uint32_t source) {
    auto* item = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
    if (!item) return nullptr;
    if (item->GetClassId() != kClassQuickItem) return nullptr;
    if (item->GetCategory() == 5 || item->GetCategory() == 8) return item;
    return nullptr;
}
EI_REPLACE_METHOD(0x0054B7C0, CUnitServer::SpellCarrier);

bool CUnitServer::IsScrollSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return false;
    auto* item = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
    if (!item || item->GetClassId() != kClassQuickItem) return false;
    if (item->GetCategory() != 5 && item->GetCategory() != 8) return false;
    return item->GetCategory() == 8;
}
EI_REPLACE_METHOD(0x0054B820, CUnitServer::IsScrollSource);

void* CUnitServer::SpellOfSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return unit->GetSpellObject(source);
    auto* item = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
    if (!item || item->GetClassId() != kClassQuickItem) return nullptr;
    if (item->GetCategory() != 5 && item->GetCategory() != 8) return nullptr;
    return item->spell_;
}
EI_REPLACE_METHOD(0x0054B880, CUnitServer::SpellOfSource);

bool CUnitServer::CanCast(const UnitAction* order) {
    std::uint32_t source = order->source;
    if (source & kQuickItemSource) {
        auto* item = static_cast<CItem*>(unit->GetQuickItem(source & ~kQuickItemSource));
        if (!item || item->GetClassId() != kClassQuickItem) return false;
        if (item->GetCategory() != 5 && item->GetCategory() != 8) return false;
        void* spell = item->spell_;
        if (!spell) return false;
        if (item->GetCategory() != 5) return true;
        // FCOMPP of the charge against the cost: less or unordered refuses.
        return !LessOrUnordered(item->charge_, FloatAt(spell, 0x40));
    }
    void* spell = unit->GetSpellObject(source);
    if (!spell) return false;
    if (unit->player != nullptr) {      // 005301A0
        float cost = FloatAt(spell, 0x40);
        float mana = ServerMana(unit);
        double needed = static_cast<double>(stats.Period20()) * cost;
        if (needed > mana) return false;
    }
    return true;
}
EI_REPLACE_METHOD(0x0054BF80, CUnitServer::CanCast);

BOOL CUnitServer::GetMovePoint(float* out) {
    if (action.kind == 1) {
        std::memcpy(out, action.point, sizeof(action.point));
        return 1;
    }
    if (action2.kind == 1) {
        std::memcpy(out, action2.point, sizeof(action2.point));
        return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0054B520, CUnitServer::GetMovePoint);

float CUnitServer::ActionDistance(const UnitAction* order) {
    if (order->flag18) return kPointTwo;
    int kind = order->kind;
    CEngineObject* target = order->target;
    if (kind >= 4 && kind <= 5) {
        if (!unit->player && Combat().naturalWeapon == kNoNaturalWeapon) return Combat().reach;
        std::uint32_t source = order->source;
        void* spell;
        if (source & kQuickItemSource) {
            CItem* carrier = SpellCarrier(source);
            if (!carrier) return kTen;
            spell = carrier->spell_;
        } else {
            spell = unit->GetSpellObject(source);
        }
        if (spell) return FloatAt(spell, 0x34);
        return kTen;
    }
    if (kind == 6) {
        if (order->source == kSkillOnTarget) {
            float part = static_cast<float>(ei::VCall<float>(target, kUnitSlotRadius) * kPointThree);
            return static_cast<float>(ei::VCall<float>(unit, kUnitSlotRadius) + part);
        }
        if (target->GetClassId() == kClassUnit) {
            auto* other = static_cast<CUnit*>(target);
            return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + kPointSix);
        }
        float own = ei::VCall<float>(unit, kUnitSlotRadius);
        return static_cast<float>((ei::VCall<float>(target, kUnitSlotRadius) + static_cast<double>(own)) - kPointOne);
    }
    // Melee (kinds below 4 and above 6).
    if (target->GetClassId() == kClassUnit) {
        auto* other = static_cast<CUnit*>(target);
        float floor = kMeleeFloor;
        if (other->player && unit->player) floor = kMeleeFloorParty;
        CItemWeapon* weapon = ActiveWeapon(unit);
        float range = weapon ? weapon->unknown_58 : Combat().reach;
        float reach = LessOrUnordered(range, floor) ? floor : range;
        return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + reach);
    }
    CItemWeapon* weapon = ActiveWeapon(unit);
    float range = weapon ? weapon->unknown_58 : Combat().reach;
    float own = ei::VCall<float>(unit, kUnitSlotRadius);
    return static_cast<float>((ei::VCall<float>(target, kUnitSlotRadius) + static_cast<double>(own)) + range);
}
EI_REPLACE_METHOD(0x0054C070, CUnitServer::ActionDistance);

bool CUnitServer::CanReachTarget(const UnitAction* order) {
    if (order->kind != 3 && order->kind != 6) return true;
    CEngineObject* target = order->target;
    if (!target) return true;
    if (target->GetClassId() != kClassUnit) return true;
    if (ei::VCall<int>(target, kUnitSlotIsDead) != 0) return true;
    auto* other = static_cast<CUnit*>(target);
    if (other->sizeClass != 0) return true;
    if (order->kind == 3) {
        CItemWeapon* weapon = ActiveWeapon(unit);
        if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return true;
    }
    double height = static_cast<double>(Field<float>(other->GetPrototype(), 0x24)) + other->positionZ;
    return LessOrUnordered(height, kOnePointFive);
}
EI_REPLACE_METHOD(0x0054C800, CUnitServer::CanReachTarget);

std::uint32_t CUnitServer::WeaponTypeMask() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return 1u << (weapon->GetType() & 31);
    int natural = Combat().naturalWeapon;
    if (natural < 7) return 1u << (natural & 31);
    return 0;
}
EI_REPLACE_METHOD(0x00550050, CUnitServer::WeaponTypeMask);

std::uint32_t CUnitServer::MoveModeMask(const MoveModeState* mode) {
    switch (static_cast<std::uint32_t>(mode->mode)) {
    case 0: return 0x30000;
    case 1: return 0x20000;
    case 2:
        if (mode->flag4 || unit->type_ != kUnitTypeHero) return 0x10000;
        return 0;
    case 4: return 0x8000;
    default: return 0;
    }
}
EI_REPLACE_METHOD(0x005500A0, CUnitServer::MoveModeMask);

int CUnitServer::Level() {
    float experience = Combat().unknown_04;
    float base = Global7C5488();
    float step = Global7C548C();
    float ratio;
    // ln(experience / base) / ln(step), as FYL2X with FLDLN2, rounded to float, then FISTP.
    __asm {
        fld experience
        fdiv base
        fldln2
        fxch st(1)
        fyl2x
        fldln2
        fld step
        fyl2x
        fdivp st(1), st
        fstp ratio
    }
    return x87::fistp(ratio);
}
EI_REPLACE_METHOD(0x00556760, CUnitServer::Level);

void CUnitServer::UpdateSpellBlockFlag() {
    if (unit->HasEffect(0x20) && Combat().naturalWeapon != kNoNaturalWeapon) unit->flags_ |= kFlagSpellBlocked;
    else unit->flags_ &= ~kFlagSpellBlocked;
}
EI_REPLACE_METHOD(0x00553220, CUnitServer::UpdateSpellBlockFlag);

void CUnitServer::GetAttackParams(AttackParams* out) {
    CUnit* self = unit;
    int keyed = IsTextKey(self->id_) & 0xFF;
    CItemWeapon* weapon = ActiveWeapon(self);
    if (keyed && weapon) {
        FpuCopy(&out->damageMin, &weapon->damage_);
        out->damageMax = weapon->damageSpread_ + weapon->damage_;
        out->weaponType = weapon->GetType();
        out->damageType = WeaponDamageType(weapon->unknown_18);
        FpuCopy(&out->speed, &weapon->unknown_34);
        float base = WeaponStat84(weapon->unknown_18);
        double bonus = StatsWeaponSkillBonus(&stats, nullptr, out->weaponType, self);
        out->attack = GameFtol(bonus + base);
        base = WeaponStat88(weapon->unknown_18);
        bonus = StatsWeaponSkillBonus(&stats, nullptr, out->weaponType, self);
        out->defence = GameFtol(bonus + base);
        out->damageType = WeaponDamageType(weapon->unknown_18);
    } else {
        std::memcpy(&out->damageMin, &Combat().damage, sizeof(float));
        out->damageMax = Combat().damageSpread + Combat().damage;
        out->attack = GameFtol(Combat().attack);
        out->defence = GameFtol(Combat().defence);
        std::memcpy(&out->speed, &Combat().speed, sizeof(float));
        if (weapon) {
            out->weaponType = weapon->GetType();
            out->damageType = WeaponDamageType(weapon->unknown_18);
        } else {
            out->weaponType = Combat().naturalWeapon;
            const char* model = self->GetPrototypeModel();
            // The first damage type with a non-zero weight; left as it was when there is none.
            for (int i = 0; i < 7; ++i) {
                float weight = Field<float>(model, 0x50 + 4 * i);
                if (weight < kZero || weight > kZero) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (out->weaponType == kWeaponBow || out->weaponType == kWeaponCrossbow) out->defence = 0;
    out->damageMax = StatsScaleDamage(&stats, nullptr, self, out->damageMax);
    out->damageMin = StatsScaleDamage(&stats, nullptr, self, out->damageMin);
    std::memcpy(&out->attackSpread, &Combat().attackSpread, sizeof(float));
    DifficultyScaleParams(out, nullptr, keyed);
}
EI_REPLACE_METHOD(0x00555A00, CUnitServer::GetAttackParams);

float CUnitServer::AttackRoll(CUnitServer* attacker, bool backstab) {
    AttackParams theirs;
    AttackParams mine;
    ei::VCall<void>(attacker, kSlotAttackParams, &theirs);
    ei::VCall<void>(this, kSlotAttackParams, &mine);
    float speedRatio = static_cast<float>(theirs.speed / (static_cast<double>(kSpeedScale) * mine.speed));
    const CUnit* self = unit;
    const CUnit* other = attacker->unit;
    double dx = static_cast<double>(self->positionX) - other->positionX;
    double dy = static_cast<double>(self->positionY) - other->positionY;
    double dz = static_cast<double>(self->positionZ) - other->positionZ;
    double distance = x87::fsqrt((dz * dz + dy * dy) + dx * dx);
    float twice = static_cast<float>(distance + distance);
    double radius = ei::VCall<float>(unit, kUnitSlotRadius) * static_cast<double>(kDistanceScale);
    float penalty = static_cast<float>(twice / (radius + radius));

    double roll = kZero;
    float spread = theirs.attackSpread;
    if (spread > kZero) {
        int sides = x87::fistp(spread);
        std::uint32_t random = GlobalRandom().Next();
        double drawn = Unsigned(random % static_cast<std::uint32_t>(sides + 1));
        roll = (drawn + drawn) - (static_cast<double>(spread) + kOne);
    }
    roll = (roll + theirs.attack) - penalty;
    if (!backstab) roll = roll + speedRatio;
    return MaxZero(static_cast<float>(roll));
}
EI_REPLACE_METHOD(0x00555BD0, CUnitServer::AttackRoll);

BOOL CUnitServer::ResolveHit(DamageInfo* hit, CUnit* attacker) {
    float roll = kHitMark;
    if (!(HasElementalDamage(hit) & 0xFF)) {
        if (NoBackstab()) hit->unknown_34 = kOne;
        if (attacker) {
            bool backstab = hit->unknown_34 > kOne;
            roll = AttackRoll(reinterpret_cast<CUnitServer*>(attacker->controller), backstab);
        }
        if (hit->unknown_38) {
            if (hit->unknown_2C == 0) roll = MaxZero(roll - kCalledShotHead);
            else if (hit->unknown_2C != 1) roll = MaxZero(roll - kTen);
        }
    }
    AttackParams mine;
    ei::VCall<void>(this, kSlotAttackParams, &mine);
    if (hit->unknown_34 > kOne) mine.defence = 0;
    float result = static_cast<double>(mine.defence) > roll ? kMissMark : kHitMark;
    std::memcpy(&hit->unknown_20, &result, sizeof(result));
    return result > kZero ? 1 : 0;
}
EI_REPLACE_METHOD(0x00553DF0, CUnitServer::ResolveHit);

BOOL CUnitServer::BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot) {
    CUnit* self = unit;
    int keyed = IsTextKey(self->id_) & 0xFF;
    std::memset(out, 0, sizeof(DamageInfo));
    out->multiplier = kOne;
    CItemWeapon* weapon = nullptr;
    if (keyed && self->activeWeapon < 4) weapon = static_cast<CItemWeapon*>(self->weapons[self->activeWeapon]);
    if (weapon) {
        weapon->GetDamageTypes(out->typeWeights);
        out->unknown_1C = weapon->unknown_4C;
        int sides = GameFtol(kFive);
        int drawn = gamecrt::rand();
        int face = ((drawn * sides) / 0x7FFF) % sides;
        int value = face + face + 2;
        float roll = static_cast<float>(static_cast<double>(value) - (static_cast<double>(kFive) + kOne));
        std::memcpy(&out->unknown_20, &roll, sizeof(roll));
        out->amount = weapon->RollDamage();
    } else {
        std::memcpy(out->typeWeights, Combat().damageWeights, sizeof(out->typeWeights));
        out->unknown_1C = 0;
        double factor = keyed ? kOne : DifficultyAttackFactor();
        float roll = static_cast<float>(factor * Combat().attack);
        std::memcpy(&out->unknown_20, &roll, sizeof(roll));
        double extra = kZero;
        float spread = Combat().damageSpread;
        if (spread < kZero || spread > kZero) {
            float scaled = spread * kHundred;
            int sides = x87::fistp(scaled);
            std::uint32_t random = GlobalRandom().Next();
            extra = Unsigned(random % static_cast<std::uint32_t>(sides + 1)) * kPercent;
        }
        out->amount = static_cast<float>(extra + Combat().damage);
    }
    out->amount = StatsScaleDamage(&stats, nullptr, self, out->amount);
    out->unknown_2C = location;
    out->unknown_30 = 0;
    if (backstab) out->unknown_34 = stats.Value6E9Arg(static_cast<int>(reinterpret_cast<std::uintptr_t>(self)));
    else out->unknown_34 = kOne;
    if (self->player && PlayerMode108(self->player) >= 2) ResetDamageInfo(out);
    out->unknown_38 = static_cast<std::uint8_t>(calledShot);
    return 1;
}
EI_REPLACE_METHOD(0x0054F630, CUnitServer::BuildAttackHit);

BOOL CUnitServer::PrepareAttack(CEngineObject* target, int location, char calledShot) {
    int backstab = UnitIsBehind(unit, nullptr, target) & 0xFF;
    if (target->GetClassId() == kClassUnit &&
        reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(target)->controller)->NoBackstab())
        backstab = 0;
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow))
        return BuildAttackHit(&pendingHit, 0, location, calledShot);
    return BuildAttackHit(&pendingHit, backstab, location, calledShot);
}
EI_REPLACE_METHOD(0x0054F580, CUnitServer::PrepareAttack);

BOOL CUnitServer::TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source) {
    void* world = ManagerContext();
    int dead = ei::VCall<int>(unit, kUnitSlotIsDead);
    bool hadPlayer = unit->player != nullptr;
    // |+0x20| below FLT_MAX (or NaN): not resolved yet.
    if (LessOrUnordered(std::bit_cast<float>(hit->unknown_20 & 0x7FFFFFFFu), kHitMark)) ResolveHit(hit, attacker);
    if (!dead) {
        static_cast<CWorldServer*>(world)->AddToUpdateMap(unit);
        if (attacker) {
            GameLog()->Put("Unit ")->Put(static_cast<long>(unit->id_))->Put(" was attacked by ")->Put(
                static_cast<long>(attacker->id_));
        } else {
            GameLog()->Put("Unit ")->Put(static_cast<long>(unit->id_))->Put(" was attacked by xz");
        }
        GameOstream* log = GameLog()->Put(" skill=");
        log->SetField4();
        log = log->Put(static_cast<double>(FloatAt(hit, 0x20)));
        float amount = hit->amount;
        log = log->Put(" damage=");
        log->SetField4();
        log->Put(static_cast<double>(amount));
        if (hit->unknown_34 > kOne) {
            float power = hit->unknown_34;
            log = GameLog()->Put(" backstab with power=");
            log->SetField4();
            log->Put(static_cast<double>(power));
        }
        if (hit->unknown_38) GameLog()->Put(" called shot");
        GameLog()->Put(" destHL=");
        GameLog()->Put(HitLocationName(hit->unknown_2C));
        GameLog()->PutChar(10)->Apply(kLogEndLine);
    }

    char* player = unit->player;
    if (player) {
        if (PlayerMode108(player) != 0) return 0;
        if (source && source->player && player != source->player) return 0;
    }

    AttackParams mine;
    ei::VCall<void>(this, kSlotAttackParams, &mine);
    if (hit->unknown_34 > kOne) mine.defence = 0;
    if (!(static_cast<double>(mine.defence) > FloatAt(hit, 0x20))) {
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(&stats) + 0x6F8) |= 0x10;
        unit->MarkChanged(4);
        CallTriggerItemSpells(1);
        CEngineObject* weapon = attacker ? attacker->GetWeapon(attacker->activeWeapon) : nullptr;
        if (StatsApplyHit(&stats, nullptr, unit, ServerStatsOf(unit), hit, weapon)) {
            if (attacker && !(HasElementalDamage(hit) & 0xFF)) {
                if (attacker->type_ != kUnitTypeHero) {
                    LastDamageSource(unit) = static_cast<std::int16_t>(attacker->unknown_18 + 0x10);
                } else {
                    auto* controller = reinterpret_cast<CUnitServer*>(attacker->controller);
                    CItemWeapon* own = ActiveWeapon(controller->unit);
                    if (own) LastDamageSource(unit) = static_cast<std::int16_t>(own->GetType());
                    else LastDamageSource(unit) = static_cast<std::int16_t>(controller->Combat().naturalWeapon);
                }
            } else {
                LastDamageSource(unit) = 7;
            }
            StatsRecompute(&stats, nullptr, ServerStatsOf(unit), unit);
            unit->MarkChanged(0);
            unit->MarkChanged(4);
            if (!dead) {
                CallOnDamaged(attacker, hit->unknown_30);
                if (ei::VCall<int>(unit, kUnitSlotIsDead) != 0 && source && source->player && !hadPlayer) {
                    if (IsMultiplayerClient() & 0xFF) {
                        WorldAwardKill(world, nullptr, nullptr, Field<int>(unit->GetPrototype(), 0x54));
                        return 1;
                    }
                    char* killer = source->player;
                    WorldAwardKill(world, nullptr, killer, Field<int>(unit->GetPrototype(), 0x54));
                }
            }
            return 1;
        }
    }
    // Missed (or the stats refused the hit).
    if (attacker && attacker->GetClassId() == kClassUnit) {
        auto* diplomacy = reinterpret_cast<CDiplomacy*>(static_cast<char*>(ManagerContext()) + 0x12C64);
        diplomacy->OnAttack(attacker, unit, 10);
    }
    RememberAttacker(reinterpret_cast<char*>(this) + 0x980, nullptr, attacker);
    return 0;
}
EI_REPLACE_METHOD(0x00553F40, CUnitServer::TakeHit);

namespace {
// One size class of 00552FB0: by the load first, then by `size`.
std::int16_t SizeClassOf(double size, float load) {
    if (load > kLoadOverloaded) return 7;
    if (load > kLoadHeavy) return size > kSizeHeavy ? 6 : 5;
    if (LessOrUnordered(size, kSizeSmall)) return 1;
    if (LessOrUnordered(size, kSizeMedium)) return 2;
    return LessOrUnordered(size, kSizeLarge) ? 3 : 4;
}
} // namespace

void CUnitServer::UpdateSizeClasses() {
    CombatStats& combat = Combat();
    if (combat.unknown_60 == 2) {
        combat.sizeClass20 = 0;
        combat.sizeClass = 0;
        return;
    }
    float load = static_cast<float>(UnitLoad(unit));
    const char* model = unit->GetPrototypeModel();
    double size = UnitSize(unit) * Field<float>(model, 0x120);
    if (unit->type_ == kUnitTypeHero) {
        float uncapped = static_cast<float>(size);
        size = LessOrUnordered(kHeroSizeCap, uncapped) ? kHeroSizeCap : uncapped;
    }
    combat.sizeClass = SizeClassOf(size, load);
    combat.sizeClass20 = SizeClassOf(size * kSizeScale20, load);
    combat.sizeClass60 = SizeClassOf(size * kSizeSmall, load);
}
EI_REPLACE_METHOD(0x00552FB0, CUnitServer::UpdateSizeClasses);

void CUnitServer::UpdateMoveFactors() {
    const char* world = static_cast<const char*>(ManagerContext());
    double factor = Field<float>(world, 0x84);
    double scaled = (factor * Combat().unknown_BC) * kWorldFactorScale;
    moveFactors[0] = kOne;
    moveFactors[2] = kOne;
    moveFactors[1] = kOne;
    double sum = scaled + (kOne - factor);
    moveFactors[3] = static_cast<float>(sum);
    moveFactors[3] = static_cast<float>(Field<float>(world, 0x88) * sum);
    if (unit->moveMode == 0) {
        moveFactors[2] = kMode0Factor2;
        moveFactors[3] = kMode0Factor3 * moveFactors[3];
    } else if (unit->moveMode == 1) {
        moveFactors[2] = kMode1Factor2;
    }
    if (state == 0) {
        if (unit->moveMode == 0) moveFactors[0] = kIdleMode0Factor0;
        else if (unit->moveMode == 1) moveFactors[0] = kIdleMode1Factor0;
        if (unit->moveMode == 3) {
            moveFactors[0] = kIdleMode3Scale * moveFactors[0];
            moveFactors[1] = kIdleMode3Factor1;
        }
    } else {
        moveFactors[0] = kZero;
    }
    if (state == 5 && (action.kind == 4 || action.kind == 5)) {
        void* spell = SpellOfSource(action.source);
        if (spell && Field<int>(static_cast<CSpell*>(spell)->GetPrototype(), 0x10) != 1)
            moveFactors[2] = kCastingFactor2 * moveFactors[2];
    }
    if (state != 0) {
        speedClass = 15;
        return;
    }
    float speed = ei::VCall<float>(unit, kUnitSlotSpeed);
    if (speed > kSpeedFast) speedClass = 0;
    else if (speed > kSizeSmall) speedClass = 1;
    else if (speed > kPointThree) speedClass = 3;
    else if (speed > kSpeedSlow) speedClass = 7;
    else speedClass = 15;
}
EI_REPLACE_METHOD(0x005536B0, CUnitServer::UpdateMoveFactors);
