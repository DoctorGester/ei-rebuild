// Combat: CUnitServer's weapon and spell queries, the attack hit (damage calculation), the attack
// roll and hit resolution. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "random.h"
#include "unit_db.h"
#include "world.h"

#include "originals_world.h"
#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_ai.h"

#include <bit>
#include <cstring>

// Values the original keeps on the FPU stack are double locals, values it stores (FSTP to memory)
// are float locals.

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

constexpr int kClassQuickItem = 0x13;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;   // UnitAction::source of a skill used on a target
constexpr int kNoNaturalWeapon = 0x10;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kFlagSpellBlocked = 0x10000000;

IsaacRandom& GlobalRandom() { return IsaacRandom::Global(); }
float Global7C5488() { return *reinterpret_cast<const float*>(0x007C5488); }
float Global7C548C() { return *reinterpret_cast<const float*>(0x007C548C); }
// Difficulty (0079BEC0) and the attack factor per difficulty (007C54AC, floats).
float DifficultyAttackFactor() {
    int difficulty = *reinterpret_cast<const int*>(0x0079BEC0);
    return reinterpret_cast<const float*>(0x007C54AC)[difficulty];
}

// The unit's server object (CUnit + 0x170): its mana.
float ServerMana(const CUnit* unit) { return static_cast<const CMapObjectServer*>(unit->companion)->vitals.mana; }

std::uint8_t PlayerGodMode(const char* player) { return reinterpret_cast<const CPlayer*>(player)->godMode_; }

// The map objects' virtual slots (map_object.h) and the server controller's (CUnitServerVirtuals).
float Radius(CEngineObject* object) { return ObjectSlots(object)->Radius(); }
int IsDead(CEngineObject* object) { return ObjectSlots(object)->IsDead(); }
void AttackParamsOf(CUnitServer* controller, AttackParams* out) { ServerSlots(controller)->GetAttackParams(out); }

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

// A CSpell's dword fields that hold floats: +0x24 (prototype +0x2C, the damage), +0x34 (+0x24,
// the range) and +0x40 (+0x18, the mana cost).
float SpellDamage(const void* spell) { return std::bit_cast<float>(static_cast<const CSpell*>(spell)->damage_); }
float SpellRange(const void* spell) { return std::bit_cast<float>(static_cast<const CSpell*>(spell)->range_); }
float SpellCost(const void* spell) { return std::bit_cast<float>(static_cast<const CSpell*>(spell)->manaCost_); }

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
// 0052E0D0. Scales AttackParams::attack and ::defence by the difficulty unless `keyed`.
// 00530A60. Is the unit behind `target` (facing test)? Only AL is meaningful.
// 005254B0. Applies `hit` (with the attacker's `weapon`, may be null) to the unit's server stats;
// 0 when it did nothing.
__declspec(naked) int __fastcall StatsApplyHit(UnitStats*, void*, CUnit*, void*, DamageInfo*, CEngineObject*)
    EI_ORIGINAL(0x005254B0)
// 00524560. Recomputes the unit's server stats (hit points and the like) after a change.
// 005911C0 (WorldAwardExperienceOriginal). The world server's experience award for a kill (`player` null in multiplayer).
// 005D7E90 (BrainSetSeenOriginal). The AI's memory of attackers (CUnitServer + 0x980): remembers `attacker`.

GameOstream* GameOstream::Put(const char* text) { return static_cast<GameOstream*>(OstreamPutString(this, nullptr, text)); }
GameOstream* GameOstream::Put(long v) { return static_cast<GameOstream*>(OstreamPutLongOriginal(this, nullptr, v)); }
GameOstream* GameOstream::Put(double v) { return static_cast<GameOstream*>(OstreamPutDoubleOriginal(this, nullptr, v)); }
GameOstream* GameOstream::PutChar(int c) { return static_cast<GameOstream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
GameOstream* GameOstream::Apply(void* manipulator) { return static_cast<GameOstream*>(OstreamManipulate(this, nullptr, manipulator)); }

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

// CUnit + 0x350 (UnitParams +0x20): what last damaged the unit: a weapon type, 0x10 + the
// attacking creature's +0x18 (its prototype index), or 7 (elemental or no attacker).
std::int16_t& LastDamageSource(CUnit* unit) { return unit->Params()->lastHitSource; }
} // namespace

float CUnitServer::AttackRange() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return weapon->reach_;
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
    double factor = Combat().rate;    // stays on the FPU stack
    if (factor < kZero || factor > kZero) factor = kFifteen / factor;
    // +0x4C of the item's kind (CItemWeapon::actionValue_ and the others' at the same place).
    float value = static_cast<float>(Unsigned(static_cast<const CItemWeapon*>(item)->actionValue_) * factor);
    return x87::fistp(value);
}
EI_REPLACE_METHOD(0x0054B760, CUnitServer::ScaledItemValue);

int CUnitServer::ScaledAttackValue(int kind, CItem* item) {
    float factor = Combat().rate;
    if (factor < kZero || factor > kZero) factor = kFifteen / factor;
    if (kind != 3) return 0;
    int value;
    if (item) value = static_cast<int>(reinterpret_cast<const itemdb::ItemRecordHead*>(item->GetPrototype())->category);
    else value = static_cast<int>(gamecrt::Ftol(Combat().attackCategory));
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
        auto value = static_cast<std::int16_t>(gamecrt::Ftol(SpellDamage(spell)));
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

bool CUnitServer::IsFighting() {
    if (action.kind == 4 || action.kind == 5) {
        void* spell = nullptr;
        if (action.source & kQuickItemSource) {
            auto* item = static_cast<CItem*>(unit->GetQuickItem(action.source & ~kQuickItemSource));
            if (item && item->GetClassId() == kClassQuickItem && (item->GetCategory() == 5 || item->GetCategory() == 8))
                spell = item->spell_;
        } else {
            spell = unit->GetSpellObject(action.source);
        }
        if (spell) {
            std::uint32_t type = static_cast<CSpell*>(spell)->prototypeId_ >> 16;
            if (type <= 8) return true;
            // Rebirth's FixPlaingBattleMusicFromAntimagic: JMP over the 0x1A test.
            if (type == 0x1A && !ei::rebirth::Enabled(ei::rebirth::Patch::FixPlaingBattleMusicFromAntimagic))
                return true;
        }
    }
    return action.kind == 3;
}
EI_REPLACE_METHOD(0x0054B580, CUnitServer::IsFighting);
EI_REBIRTH_IMPLEMENTS(0x0054B580, ei::rebirth::Patch::FixPlaingBattleMusicFromAntimagic);

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
        return !LessOrUnordered(item->charge_, SpellCost(spell));
    }
    void* spell = unit->GetSpellObject(source);
    if (!spell) return false;
    if (unit->player != nullptr) {      // 005301A0
        float cost = SpellCost(spell);
        float mana = ServerMana(unit);
        double needed = static_cast<double>(stats.Period20()) * cost;
        if (needed > mana) return false;
    }
    return true;
}
EI_REPLACE_METHOD(0x0054BF80, CUnitServer::CanCast);

BOOL CUnitServer::GetMovePoint(float* out) {
    if (action.kind == 1) {
        std::memcpy(out, &action.point, sizeof(action.point));
        return 1;
    }
    if (action2.kind == 1) {
        std::memcpy(out, &action2.point, sizeof(action2.point));
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
        if (spell) return SpellRange(spell);
        return kTen;
    }
    if (kind == 6) {
        if (order->source == kSkillOnTarget) {
            float part = static_cast<float>(Radius(target) * kPointThree);
            return static_cast<float>(Radius(unit) + part);
        }
        if (target->GetClassId() == kClassUnit) {
            auto* other = static_cast<CUnit*>(target);
            return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + kPointSix);
        }
        float own = Radius(unit);
        return static_cast<float>((Radius(target) + static_cast<double>(own)) - kPointOne);
    }
    // Melee (kinds below 4 and above 6).
    if (target->GetClassId() == kClassUnit) {
        auto* other = static_cast<CUnit*>(target);
        float floor = kMeleeFloor;
        if (other->player && unit->player) floor = kMeleeFloorParty;
        CItemWeapon* weapon = ActiveWeapon(unit);
        float range = weapon ? weapon->reach_ : Combat().reach;
        float reach = LessOrUnordered(range, floor) ? floor : range;
        return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + reach);
    }
    CItemWeapon* weapon = ActiveWeapon(unit);
    float range = weapon ? weapon->reach_ : Combat().reach;
    float own = Radius(unit);
    return static_cast<float>((Radius(target) + static_cast<double>(own)) + range);
}
EI_REPLACE_METHOD(0x0054C070, CUnitServer::ActionDistance);

bool CUnitServer::CanReachTarget(const UnitAction* order) {
    if (order->kind != 3 && order->kind != 6) return true;
    CEngineObject* target = order->target;
    if (!target) return true;
    if (target->GetClassId() != kClassUnit) return true;
    if (IsDead(target) != 0) return true;
    auto* other = static_cast<CUnit*>(target);
    if (other->sizeClass != 0) return true;
    if (order->kind == 3) {
        CItemWeapon* weapon = ActiveWeapon(unit);
        if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return true;
    }
    float offset = std::bit_cast<float>(reinterpret_cast<const UnitPrototypeRecord*>(other->GetPrototype())->height);
    double height = static_cast<double>(offset) + other->position.z;
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
    float experience = Combat().experience;
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
        out->damageType = WeaponDamageType(weapon->prototypeId_);
        FpuCopy(&out->speed, &weapon->weight_);
        float base = WeaponStat84(weapon->prototypeId_);
        double bonus = StatsWeaponSkillBonus(&stats, nullptr, out->weaponType, self);
        out->attack = static_cast<int>(gamecrt::Ftol(bonus + base));
        base = WeaponStat88(weapon->prototypeId_);
        bonus = StatsWeaponSkillBonus(&stats, nullptr, out->weaponType, self);
        out->defence = static_cast<int>(gamecrt::Ftol(bonus + base));
        out->damageType = WeaponDamageType(weapon->prototypeId_);
    } else {
        std::memcpy(&out->damageMin, &Combat().damage, sizeof(float));
        out->damageMax = Combat().damageSpread + Combat().damage;
        out->attack = static_cast<int>(gamecrt::Ftol(Combat().attack));
        out->defence = static_cast<int>(gamecrt::Ftol(Combat().defence));
        std::memcpy(&out->speed, &Combat().speed, sizeof(float));
        if (weapon) {
            out->weaponType = weapon->GetType();
            out->damageType = WeaponDamageType(weapon->prototypeId_);
        } else {
            out->weaponType = Combat().naturalWeapon;
            const auto* model = reinterpret_cast<const UnitModelRecord*>(self->GetPrototypeModel());
            // The first damage type with a non-zero weight; left as it was when there is none.
            for (int i = 0; i < 7; ++i) {
                float weight = std::bit_cast<float>(model->damageWeights[i]);
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
    AttackParamsOf(attacker, &theirs);
    AttackParamsOf(this, &mine);
    float speedRatio = static_cast<float>(theirs.speed / (static_cast<double>(kSpeedScale) * mine.speed));
    const CUnit* self = unit;
    const CUnit* other = attacker->unit;
    double dx = static_cast<double>(self->position.x) - other->position.x;
    double dy = static_cast<double>(self->position.y) - other->position.y;
    double dz = static_cast<double>(self->position.z) - other->position.z;
    double distance = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz));
    float twice = static_cast<float>(distance + distance);
    double radius = Radius(unit) * static_cast<double>(kDistanceScale);
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
        if (NoBackstab()) hit->backstabFactor = kOne;
        if (attacker) {
            bool backstab = hit->backstabFactor > kOne;
            roll = AttackRoll(reinterpret_cast<CUnitServer*>(attacker->controller), backstab);
        }
        if (hit->calledShot) {
            if (hit->location == 0) roll = MaxZero(roll - kCalledShotHead);
            else if (hit->location != 1) roll = MaxZero(roll - kTen);
        }
    }
    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > kOne) mine.defence = 0;
    float result = static_cast<double>(mine.defence) > roll ? kMissMark : kHitMark;
    std::memcpy(&hit->attackRoll, &result, sizeof(result));
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
        out->weaponValue = weapon->actionValue_;
        int sides = static_cast<int>(gamecrt::Ftol(kFive));
        int drawn = gamecrt::rand();
        int face = ((drawn * sides) / 0x7FFF) % sides;
        int value = face + face + 2;
        float roll = static_cast<float>(static_cast<double>(value) - (static_cast<double>(kFive) + kOne));
        std::memcpy(&out->attackRoll, &roll, sizeof(roll));
        out->amount = static_cast<float>(weapon->RollDamage());   // FSTP float, as the original
    } else {
        std::memcpy(out->typeWeights, Combat().damageWeights, sizeof(out->typeWeights));
        out->weaponValue = 0;
        double factor = keyed ? kOne : DifficultyAttackFactor();
        float roll = static_cast<float>(factor * Combat().attack);
        std::memcpy(&out->attackRoll, &roll, sizeof(roll));
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
    out->location = location;
    out->hitKind = 0;
    if (backstab) out->backstabFactor = static_cast<float>(stats.Value6E9Arg(static_cast<int>(reinterpret_cast<std::uintptr_t>(self))));
    else out->backstabFactor = kOne;
    if (self->player && PlayerGodMode(self->player) >= 2) ResetDamageInfo(out);
    out->calledShot = static_cast<std::uint8_t>(calledShot);
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
    int dead = IsDead(unit);
    bool hadPlayer = unit->player != nullptr;
    // |+0x20| below FLT_MAX (or NaN): not resolved yet.
    if (LessOrUnordered(std::bit_cast<float>(hit->attackRoll & 0x7FFFFFFFu), kHitMark)) ResolveHit(hit, attacker);
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
        log = log->Put(static_cast<double>(std::bit_cast<float>(hit->attackRoll)));
        float amount = hit->amount;
        log = log->Put(" damage=");
        log->SetField4();
        log->Put(static_cast<double>(amount));
        if (hit->backstabFactor > kOne) {
            float power = hit->backstabFactor;
            log = GameLog()->Put(" backstab with power=");
            log->SetField4();
            log->Put(static_cast<double>(power));
        }
        if (hit->calledShot) GameLog()->Put(" called shot");
        GameLog()->Put(" destHL=");
        GameLog()->Put(HitLocationName(hit->location));
        GameLog()->PutChar(10)->Apply(kLogEndLine);
    }

    char* player = unit->player;
    if (player) {
        if (PlayerGodMode(player) != 0) return 0;
        if (source && source->player && player != source->player) return 0;
    }

    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > kOne) mine.defence = 0;
    if (!(static_cast<double>(mine.defence) > std::bit_cast<float>(hit->attackRoll))) {
        stats.flags |= 0x10;
        unit->MarkChanged(4);
        CallTriggerItemSpells(1);
        CEngineObject* weapon = attacker ? attacker->GetWeapon(attacker->activeWeapon) : nullptr;
        if (StatsApplyHit(&stats, nullptr, unit, ServerStatsOf(unit), hit, weapon)) {
            if (attacker && !(HasElementalDamage(hit) & 0xFF)) {
                if (attacker->type_ != kUnitTypeHero) {
                    LastDamageSource(unit) = static_cast<std::int16_t>(attacker->prototypeId_ + 0x10);
                } else {
                    auto* controller = reinterpret_cast<CUnitServer*>(attacker->controller);
                    CItemWeapon* own = ActiveWeapon(controller->unit);
                    if (own) LastDamageSource(unit) = static_cast<std::int16_t>(own->GetType());
                    else LastDamageSource(unit) = static_cast<std::int16_t>(controller->Combat().naturalWeapon);
                }
            } else {
                LastDamageSource(unit) = 7;
            }
            UnitStatsRecompute(&stats, nullptr, ServerStatsOf(unit), unit);
            unit->MarkChanged(0);
            unit->MarkChanged(4);
            if (!dead) {
                CallOnDamaged(attacker, hit->hitKind);
                if (IsDead(unit) != 0 && source && source->player && !hadPlayer) {
                    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
                    if (IsMultiplayerClient() & 0xFF) {
                        WorldAwardExperienceOriginal(world, nullptr, nullptr, static_cast<int>(prototype->experienceValue));
                        return 1;
                    }
                    char* killer = source->player;
                    WorldAwardExperienceOriginal(world, nullptr, reinterpret_cast<WorldPlayer*>(killer), static_cast<int>(prototype->experienceValue));
                }
            }
            return 1;
        }
    }
    // Missed (or the stats refused the hit).
    if (attacker && attacker->GetClassId() == kClassUnit) {
        auto* diplomacy = &static_cast<CWorldServer*>(ManagerContext())->diplomacy_;
        diplomacy->OnAttack(attacker, unit, 10);
    }
    BrainSetSeenOriginal(&brain, nullptr, attacker);
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
    if (combat.bodyKind == 2) {
        combat.sizeClass20 = 0;
        combat.sizeClass = 0;
        return;
    }
    float load = static_cast<float>(UnitLoad(unit));
    const auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
    double size = UnitSize(unit) * std::bit_cast<float>(model->sizeScale);
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
    const auto* world = static_cast<const CWorldServer*>(ManagerContext());
    double factor = std::bit_cast<float>(world->nightLevel_);
    double scaled = (factor * Combat().daySightPercent) * kWorldFactorScale;
    moveFactors[0] = kOne;
    moveFactors[2] = kOne;
    moveFactors[1] = kOne;
    double sum = scaled + (kOne - factor);
    moveFactors[3] = static_cast<float>(sum);
    moveFactors[3] = static_cast<float>(std::bit_cast<float>(world->weatherFactor_) * sum);
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
        if (spell && reinterpret_cast<const itemdb::SpellRecord*>(static_cast<CSpell*>(spell)->GetPrototype())->castType != 1)
            moveFactors[2] = kCastingFactor2 * moveFactors[2];
    }
    if (state != 0) {
        speedClass = 15;
        return;
    }
    float speed = static_cast<float>(ObjectSlots(unit)->MoveSpeed());
    if (speed > kSpeedFast) speedClass = 0;
    else if (speed > kSizeSmall) speedClass = 1;
    else if (speed > kPointThree) speedClass = 3;
    else if (speed > kSpeedSlow) speedClass = 7;
    else speedClass = 15;
}
EI_REPLACE_METHOD(0x005536B0, CUnitServer::UpdateMoveFactors);
