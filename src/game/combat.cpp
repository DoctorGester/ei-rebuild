// Combat: CUnitServer's weapon and spell queries, the attack hit (damage calculation), the attack
// roll and hit resolution. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "console.h"
#include "options.h"
#include "combat_internal.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "random.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/crt.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"
#include "lib/float_select.h"

#include <bit>
#include <cstring>

// Values the original keeps unrounded are double locals, values it stores (to memory) are float
// locals.

namespace {
constexpr float kPointOne = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kPointOne) == 0x3DCCCCCDu);
constexpr float kPointTwo = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kPointTwo) == 0x3E4CCCCDu);
constexpr float kPointThree = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kPointThree) == 0x3E99999Au);
constexpr float kPointSix = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kPointSix) == 0x3F19999Au);
constexpr float kOnePointFive = 1.5f;
constexpr float kFive = 5.0f;
constexpr float kTen = 10.0f;
constexpr float kDistanceScale = 10.0f;
constexpr float kFifteen = 15.0f;
constexpr float kSpeedScale = 20.0f;
constexpr float kCalledShotHead = 25.0f;
constexpr float kHundred = 100.0f;
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kPercent) == 0x3C23D70Au);
constexpr float kHitMark = 3.4028235e38f;       // FLT_MAX
static_assert(std::bit_cast<std::uint32_t>(kHitMark) == 0x7F7FFFFFu);
constexpr float kMissMark = -3.4028235e38f;      // -FLT_MAX
static_assert(std::bit_cast<std::uint32_t>(kMissMark) == 0xFF7FFFFFu);
constexpr float kMeleeFloor = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kMeleeFloor) == 0x3F19999Au);
constexpr float kMeleeFloorParty = 0.20000002f; // 0.20000002 (sic)
static_assert(std::bit_cast<std::uint32_t>(kMeleeFloorParty) == 0x3E4CCCCEu);

constexpr float kHeroSizeCap = 1.89f;
static_assert(std::bit_cast<std::uint32_t>(kHeroSizeCap) == 0x3FF1EB85u);
constexpr float kLoadOverloaded = 1.5f;
constexpr float kLoadHeavy = 0.85f;
static_assert(std::bit_cast<std::uint32_t>(kLoadHeavy) == 0x3F59999Au);
constexpr float kSizeHeavy = 1.7f;
static_assert(std::bit_cast<std::uint32_t>(kSizeHeavy) == 0x3FD9999Au);
constexpr float kSizeSmall = kPointSix;
constexpr float kSizeMedium = 1.2f;
static_assert(std::bit_cast<std::uint32_t>(kSizeMedium) == 0x3F99999Au);
constexpr float kSizeLarge = 1.9f;
static_assert(std::bit_cast<std::uint32_t>(kSizeLarge) == 0x3FF33333u);
constexpr float kSizeScale20 = kPointTwo;                           // 0.2
constexpr float kWorldFactorScale = kPercent;                       // 0.01
constexpr float kMode0Factor2 = 0.5f;
constexpr float kMode1Factor2 = 0.75f;
constexpr float kMode0Factor3 = 1.0f;
constexpr float kIdleMode0Factor0 = 0.4f;
static_assert(std::bit_cast<std::uint32_t>(kIdleMode0Factor0) == 0x3ECCCCCDu);
constexpr float kIdleMode1Factor0 = kPointTwo;                      // 0.2
constexpr float kIdleMode3Scale = 2.0f;
constexpr float kIdleMode3Factor1 = 0.5f;
constexpr float kCastingFactor2 = 1.5f;
constexpr float kSpeedFast = kSizeMedium;                           // 1.2
constexpr float kSpeedSlow = 0.15f;
static_assert(std::bit_cast<std::uint32_t>(kSpeedSlow) == 0x3E19999Au);

constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr int kNoNaturalWeapon = 0x10;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kFlagSpellBlocked = 0x10000000;

float DifficultyAttackFactor() {
    int difficulty = options::g_options->difficulty_;
    return g_logicSettings.attack[difficulty];
}

float ServerMana(const CUnit* unit) { return unit->ServerCompanion()->vitals.mana; }

std::uint8_t PlayerGodMode(const CPlayer* player) { return player->godMode_; }

float Radius(CMapObject* object) { return object->Radius(); }
int IsDead(CMapObject* object) { return object->IsDead(); }
void AttackParamsOf(CUnitServer* controller, AttackParams* out) { controller->GetAttackParams(out); }

// The original's branch-free max(0, v): v when 0 < v or v is NaN, else +0.

// A zero-extended dword converted as a 64-bit integer (exact; a plain uint32 conversion would add 2^32 in a step
// the precision control rounds).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

float SpellDamage(const CSpell* spell) { return spell->damage_; }
float SpellRange(const CSpell* spell) { return spell->range_; }
float SpellCost(const CSpell* spell) { return spell->manaCost_; }

CItemWeapon* ActiveWeapon(CUnit* unit) { return unit->GetWeapon(unit->activeWeapon); }

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

// What last damaged the unit: a weapon type, 0x10 + the attacker's prototype index, or 7 (elemental or no
// attacker).
std::int16_t& LastDamageSource(CUnit* unit) { return unit->params.lastHitSource; }

// One size class of the size computation: by the load first, then by `size`.
std::int16_t SizeClassOf(double size, float load) {
    if (load > kLoadOverloaded) return 7;
    if (load > kLoadHeavy) return size > kSizeHeavy ? 6 : 5;
    if (size >= kSizeLarge) return 4;
    if (size >= kSizeMedium) return 3;
    return size >= kSizeSmall ? 2 : 1;   // also for a NaN size
}

} // namespace



float CUnitServer::AttackRange() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return weapon->reach_;
    return stats.combat[UnitStats::kReach];
}

BOOL CUnitServer::HasRangedWeapon() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (!weapon) return 0;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow ? 1 : 0;
}

BOOL CUnitServer::AttacksFromRange() {
    if (!ActiveWeapon(unit)) {
        return stats.naturalWeapon != kNoNaturalWeapon && stats.combat[UnitStats::kReach] > 1.0f ? 1 : 0;
    }
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (!weapon) return 0;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow ? 1 : 0;
}

void CUnitServer::GetDamageByType(DamageWords* out) {
    CUnit* self = unit;
    CItemWeapon* weapon;
    if (self->player && self->activeWeapon < 4 &&
        (weapon = AsWeapon(self->weapons[self->activeWeapon])) != nullptr) {
        float total = weapon->damageSpread_ + weapon->damage_;
        float scale = ScaleDamageByEffects(self, total);   // (not read)
        float weights[7];
        weapon->GetDamageTypes(weights);
        for (int i = 0; i < 7; ++i) {
            float value = scale * weights[i];
            out->words[1 + i] = static_cast<std::uint16_t>(x87::fistp(value));
        }
        return;
    }
    float total = stats.damageSpread + stats.damage;
    float scale = ScaleDamageByEffects(self, total);
    for (int i = 0; i < 7; ++i) {
        float value = scale * stats.damageWeights[i];
        out->words[1 + i] = static_cast<std::uint16_t>(x87::fistp(value));
    }
}

void CUnitServer::GetSpellDamage(std::uint32_t source, DamageWords* out) {
    CSpell* spell = SpellOfSource(source);
    *out = DamageWords{};
    if (spell) {
        auto value = static_cast<std::uint16_t>(libcrt::Ftol(SpellDamage(spell)));
        int type = spell->GetDamageType();
        out->words[1 + type] = value;
    }
    out->words[0] = 0x7FFF;
}

CItem* CUnitServer::SpellCarrier(std::uint32_t source) {
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item) return nullptr;
    if (item->GetClassId() != kClassItemQuick) return nullptr;
    if (item->GetCategory() == kCategoryWand || item->GetCategory() == kCategoryPotion) return item;
    return nullptr;
}

bool CUnitServer::IsScrollSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return false;
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item || item->GetClassId() != kClassItemQuick) return false;
    if (item->GetCategory() != kCategoryWand && item->GetCategory() != kCategoryPotion) return false;
    return item->GetCategory() == kCategoryPotion;
}

CSpell* CUnitServer::SpellOfSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return unit->GetSpellObject(source);
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item || item->GetClassId() != kClassItemQuick) return nullptr;
    if (item->GetCategory() != kCategoryWand && item->GetCategory() != kCategoryPotion) return nullptr;
    return item->spell_;
}

bool CUnitServer::IsFighting() {
    if (action.kind == kOrderCastAt || action.kind == kOrderCastOn) {
        CSpell* spell = nullptr;
        if (action.source & kQuickItemSource) {
            auto* item = unit->GetQuickItem(action.source & ~kQuickItemSource);
            if (item && item->GetClassId() == kClassItemQuick && (item->GetCategory() == kCategoryWand || item->GetCategory() == kCategoryPotion))
                spell = item->spell_;
        } else {
            spell = unit->GetSpellObject(action.source);
        }
        if (spell) {
            std::uint32_t type = spell->prototypeId_ >> 16;
            if (type <= 8) return true;
            // Rebirth's FixPlaingBattleMusicFromAntimagic skips the 0x1A test.
            if (type == 0x1A && !ei::rebirth::Enabled(ei::rebirth::Patch::FixPlaingBattleMusicFromAntimagic))
                return true;
        }
    }
    return action.kind == kOrderAttack;
}

bool CUnitServer::CanCast(const AIOrder* order) {
    std::uint32_t source = order->source;
    if (source & kQuickItemSource) {
        auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
        if (!item || item->GetClassId() != kClassItemQuick) return false;
        if (item->GetCategory() != kCategoryWand && item->GetCategory() != kCategoryPotion) return false;
        CSpell* spell = item->spell_;
        if (!spell) return false;
        if (item->GetCategory() != kCategoryWand) return true;
        // The charge against the cost: less (or unordered) refuses.
        return item->charge_ >= SpellCost(spell);
    }
    CSpell* spell = unit->GetSpellObject(source);
    if (!spell) return false;
    if (unit->player != nullptr) {
        float cost = SpellCost(spell);
        float mana = ServerMana(unit);
        double needed = static_cast<double>(stats.Period20()) * cost;
        if (needed > mana) return false;
    }
    return true;
}

BOOL CUnitServer::GetMovePoint(float* out) {
    if (action.kind == kOrderMove) {
        lib::StoreBytes(out, action.point);
        return 1;
    }
    if (action2.kind == kOrderMove) {
        lib::StoreBytes(out, action2.point);
        return 1;
    }
    return 0;
}

float CUnitServer::ActionDistance(const AIOrder* order) {
    if (order->pointIsUseSpot) return kPointTwo;
    int kind = order->kind;
    CMapObject* target = order->target;
    if (kind == kOrderCastAt || kind == kOrderCastOn) {
        if (!unit->player && stats.naturalWeapon == kNoNaturalWeapon) return stats.combat[UnitStats::kReach];
        std::uint32_t source = order->source;
        CSpell* spell;
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
        if (order->source == kSourceSearchBody) {
            float part = static_cast<float>(Radius(target) * kPointThree);
            return static_cast<float>(Radius(unit) + part);
        }
        if (CUnit* other = UnitOrNull(target)) {
            return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + kPointSix);
        }
        float own = Radius(unit);
        return static_cast<float>((Radius(target) + static_cast<double>(own)) - kPointOne);
    }
    if (CUnit* other = UnitOrNull(target)) {
        float floor = kMeleeFloor;
        if (other->player && unit->player) floor = kMeleeFloorParty;
        CItemWeapon* weapon = ActiveWeapon(unit);
        float range = weapon ? weapon->reach_ : stats.combat[UnitStats::kReach];
        float reach = range >= floor ? range : floor;
        return static_cast<float>((static_cast<double>(unit->standingHeight) + other->standingHeight) + reach);
    }
    CItemWeapon* weapon = ActiveWeapon(unit);
    float range = weapon ? weapon->reach_ : stats.combat[UnitStats::kReach];
    float own = Radius(unit);
    return static_cast<float>((Radius(target) + static_cast<double>(own)) + range);
}

bool CUnitServer::CanReachTarget(const AIOrder* order) {
    if (order->kind != kOrderAttack && order->kind != kOrderUse) return true;
    CUnit* other = UnitOrNull(order->target);
    if (!other) return true;
    if (IsDead(other) != 0) return true;
    if (other->sizeClass != 0) return true;
    if (order->kind == kOrderAttack) {
        CItemWeapon* weapon = ActiveWeapon(unit);
        if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return true;
    }
    float offset = other->GetPrototype()->height;
    double height = static_cast<double>(offset) + other->localPosition_.z;
    EI_NAN_AUDIT(height);
    return height < kOnePointFive;
}

std::uint32_t CUnitServer::WeaponTypeMask() {
    CItemWeapon* weapon = ActiveWeapon(unit);
    if (weapon) return 1u << (weapon->GetType() & 31);
    int natural = stats.naturalWeapon;
    if (natural < 7) return 1u << (natural & 31);
    return 0;
}

std::uint32_t CUnitServer::MoveModeMask(const MoveModeState* mode) {
    switch (static_cast<std::uint32_t>(mode->mode)) {
    case 0: return 0x30000;
    case 1: return 0x20000;
    case 2:
        if (mode->onGuard || unit->type_ != kUnitTypeHero) return 0x10000;
        return 0;
    case 4: return 0x8000;
    default: return 0;
    }
}

int CUnitServer::Level() {
    float experience = stats.experience;
    float base = g_logicSettings.considerModifier;
    float step = g_logicSettings.considerLogBase;
    // ln(experience / base) / ln(step) as the x87 computes it, rounded to float, then to the nearest integer.
    const float ratio = static_cast<float>(x87::flog(static_cast<double>(experience) / base) / x87::flog(step));
    return x87::fistp(ratio);
}

void CUnitServer::EffectsChanged() {
    if (unit->HasEffect(0x20) && stats.naturalWeapon != kNoNaturalWeapon) unit->flags_ |= kFlagSpellBlocked;
    else unit->flags_ &= ~kFlagSpellBlocked;
}

void CUnitServer::GetAttackParams(AttackParams* out) {
    CUnit* self = unit;
    int keyed = IsTextKey(self->id_) & 0xFF;
    CItemWeapon* weapon = ActiveWeapon(self);
    if (keyed && weapon) {
        out->damageMin = weapon->damage_;
        out->damageMax = weapon->damageSpread_ + weapon->damage_;
        out->weaponType = weapon->GetType();
        out->damageType = WeaponDamageType(weapon->prototypeId_);
        out->speed = weapon->weight_;
        float base = WeaponAttackBonus(weapon->prototypeId_);
        double bonus = stats.WeaponSkillBonus(out->weaponType, self);
        out->attack = static_cast<int>(libcrt::Ftol(bonus + base));
        base = WeaponDefenceBonus(weapon->prototypeId_);
        bonus = stats.WeaponSkillBonus(out->weaponType, self);
        out->defence = static_cast<int>(libcrt::Ftol(bonus + base));
        out->damageType = WeaponDamageType(weapon->prototypeId_);
    } else {
        out->damageMin = stats.damage;
        out->damageMax = stats.damageSpread + stats.damage;
        out->attack = static_cast<int>(libcrt::Ftol(stats.combat[UnitStats::kAttack]));
        out->defence = static_cast<int>(libcrt::Ftol(stats.combat[UnitStats::kDefence]));
        out->speed = stats.combat[UnitStats::kSpeed];
        if (weapon) {
            out->weaponType = weapon->GetType();
            out->damageType = WeaponDamageType(weapon->prototypeId_);
        } else {
            out->weaponType = stats.naturalWeapon;
            const auto* model = self->GetPrototypeModel();
            // The first damage type with a non-zero weight; left as it was when there is none.
            for (int i = 0; i < 7; ++i) {
                float weight = model->damageWeights[i];
                EI_NAN_AUDIT(weight);
                if (weight != 0.0f) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (out->weaponType == kWeaponBow || out->weaponType == kWeaponCrossbow) out->defence = 0;
    out->damageMax = ScaleDamageByEffects(self, out->damageMax);
    out->damageMin = ScaleDamageByEffects(self, out->damageMin);
    out->attackSpread = stats.combat[UnitStats::kAttackSpread];
    out->ScaleForDifficulty(keyed);
}

float CUnitServer::AttackRoll(CUnitServer* attacker, bool backstab) {
    AttackParams theirs;
    AttackParams mine;
    AttackParamsOf(attacker, &theirs);
    AttackParamsOf(this, &mine);
    float speedRatio = static_cast<float>(theirs.speed / (static_cast<double>(kSpeedScale) * mine.speed));
    const CUnit* self = unit;
    const CUnit* other = attacker->unit;
    double dx = static_cast<double>(self->localPosition_.x) - other->localPosition_.x;
    double dy = static_cast<double>(self->localPosition_.y) - other->localPosition_.y;
    double dz = static_cast<double>(self->localPosition_.z) - other->localPosition_.z;
    double distance = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz));
    float twice = static_cast<float>(distance + distance);
    double radius = Radius(unit) * static_cast<double>(kDistanceScale);
    float penalty = static_cast<float>(twice / (radius + radius));

    double roll = 0.0;
    float spread = theirs.attackSpread;
    if (spread > 0.0f) {
        int sides = x87::fistp(spread);
        std::uint32_t random = g_random.Next();
        double drawn = Unsigned(random % static_cast<std::uint32_t>(sides + 1));
        roll = (drawn + drawn) - (static_cast<double>(spread) + 1.0f);
    }
    roll = (roll + theirs.attack) - penalty;
    if (!backstab) roll = roll + speedRatio;
    return MaxKeepB(0.0f, static_cast<float>(roll));
}

BOOL CUnitServer::ResolveHit(DamageInfo* hit, CUnit* attacker) {
    float roll = kHitMark;
    if (!(HasElementalDamage(hit) & 0xFF)) {
        if (moveMode2.onGuard) hit->backstabFactor = 1.0f;
        if (attacker) {
            bool backstab = hit->backstabFactor > 1.0f;
            roll = AttackRoll(attacker->ServerController(), backstab);
        }
        if (hit->calledShot) {
            if (hit->location == 0) roll = MaxKeepB(0.0f, roll - kCalledShotHead);
            else if (hit->location != 1) roll = MaxKeepB(0.0f, roll - kTen);
        }
    }
    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > 1.0f) mine.defence = 0;
    float result = static_cast<double>(mine.defence) > roll ? kMissMark : kHitMark;
    hit->attackRoll = result;
    return result > 0.0f ? 1 : 0;
}

BOOL CUnitServer::BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot) {
    CUnit* self = unit;
    int keyed = IsTextKey(self->id_) & 0xFF;
    std::memset(out, 0, sizeof(DamageInfo));
    out->multiplier = 1.0f;
    CItemWeapon* weapon = nullptr;
    if (keyed && self->activeWeapon < 4) weapon = AsWeapon(self->weapons[self->activeWeapon]);
    if (weapon) {
        weapon->GetDamageTypes(out->typeWeights);
        out->weaponValue = weapon->actionValue_;
        int sides = static_cast<int>(libcrt::Ftol(kFive));
        int drawn = libcrt::Rand();
        int face = ((drawn * sides) / 0x7FFF) % sides;
        int value = face + face + 2;
        float roll = static_cast<float>(static_cast<double>(value) - (static_cast<double>(kFive) + 1.0f));
        out->attackRoll = roll;
        out->amount = static_cast<float>(weapon->RollDamage());   // stored as a float, as the original
    } else {
        std::memcpy(out->typeWeights, stats.damageWeights, sizeof(out->typeWeights));
        out->weaponValue = 0;
        double factor = keyed ? 1.0f : DifficultyAttackFactor();
        float roll = static_cast<float>(factor * stats.combat[UnitStats::kAttack]);
        out->attackRoll = roll;
        double extra = 0.0;
        float spread = stats.damageSpread;
        EI_NAN_AUDIT(spread);
        if (spread != 0.0f) {
            float scaled = spread * kHundred;
            int sides = x87::fistp(scaled);
            std::uint32_t random = g_random.Next();
            extra = Unsigned(random % static_cast<std::uint32_t>(sides + 1)) * kPercent;
        }
        out->amount = static_cast<float>(extra + stats.damage);
    }
    out->amount = ScaleDamageByEffects(self, out->amount);
    out->location = location;
    out->hitKind = 0;
    if (backstab) out->backstabFactor = static_cast<float>(stats.BackstabFactor());
    else out->backstabFactor = 1.0f;
    if (self->player && PlayerGodMode(self->player) >= 2) ResetDamageInfo(out);
    out->calledShot = static_cast<std::uint8_t>(calledShot);
    return 1;
}

BOOL CUnitServer::TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source) {
    CWorldServer* world = ServerWorld();
    int dead = IsDead(unit);
    bool hadPlayer = unit->player != nullptr;
    // |attackRoll| below FLT_MAX: not resolved yet.
    float roll = std::bit_cast<float>(std::bit_cast<std::uint32_t>(hit->attackRoll) & 0x7FFFFFFFu);
    EI_NAN_AUDIT(roll);
    if (roll < kHitMark) ResolveHit(hit, attacker);
    if (!dead) {
        world->AddToUpdateMap(unit);
        if (attacker) {
            *kCombatLog << "Unit " << static_cast<long>(unit->id_) << " was attacked by " << static_cast<long>(attacker->id_);
        } else {
            *kCombatLog << "Unit " << static_cast<long>(unit->id_) << " was attacked by xz";
        }
        *kCombatLog << " skill=" << hit->attackRoll;
        float amount = hit->amount;
        *kCombatLog << " damage=" << amount;
        if (hit->backstabFactor > 1.0f) {
            float power = hit->backstabFactor;
            *kCombatLog << " backstab with power=" << power;
        }
        if (hit->calledShot) *kCombatLog << " called shot";
        console::EndLine(*kCombatLog << " destHL=" << HitLocationName(hit->location));
    }

    CPlayer* player = unit->player;
    if (player) {
        if (PlayerGodMode(player) != 0) return 0;
        if (source && source->player && player != source->player) return 0;
    }

    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > 1.0f) mine.defence = 0;
    EI_NAN_AUDIT(hit->attackRoll);
    if (static_cast<double>(mine.defence) <= hit->attackRoll) {
        stats.flags |= 0x10;
        unit->MarkChanged(4);
        TriggerItemSpells(1);
        CItem* weapon = attacker ? attacker->GetWeapon(attacker->activeWeapon) : nullptr;
        if (stats.ApplyHit(unit, ServerStatsOf(unit), hit, weapon)) {
            if (attacker && !(HasElementalDamage(hit) & 0xFF)) {
                if (attacker->type_ != kUnitTypeHero) {
                    LastDamageSource(unit) = static_cast<std::int16_t>(attacker->prototypeId_ + 0x10);
                } else {
                    auto* controller = attacker->ServerController();
                    CItemWeapon* own = ActiveWeapon(controller->unit);
                    if (own) LastDamageSource(unit) = static_cast<std::int16_t>(own->GetType());
                    else LastDamageSource(unit) = static_cast<std::int16_t>(controller->stats.naturalWeapon);
                }
            } else {
                LastDamageSource(unit) = 7;
            }
            stats.Recompute(ServerStatsOf(unit), unit);
            unit->MarkChanged(0);
            unit->MarkChanged(4);
            if (!dead) {
                OnDamaged(attacker, hit->hitKind);
                if (IsDead(unit) != 0 && source && source->player && !hadPlayer) {
                    const auto* prototype = unit->GetPrototype();
                    if (IsMultiplayerClient() & 0xFF) {
                        world->AwardExperience(nullptr, prototype->experienceValue);
                        return 1;
                    }
                    CPlayer* killer = source->player;
                    world->AwardExperience(killer, prototype->experienceValue);
                }
            }
            return 1;
        }
    }
    if (attacker && attacker->GetClassId() == kClassUnit) {
        auto* diplomacy = &ServerWorld()->diplomacy_;
        diplomacy->OnAttack(attacker, unit);
    }
    brain.SetSeen(attacker);
    return 0;
}

void CUnitServer::UpdateSizeClasses() {
    UnitStats& combat = stats;
    if (combat.movementKind == 2) {
        combat.sizeClass20 = 0;
        combat.sizeClass = 0;
        return;
    }
    float load = static_cast<float>(unit->BodyExtent());
    const auto* model = unit->GetPrototypeModel();
    double size = unit->DoubleHeight() * model->sizeScale;
    if (unit->type_ == kUnitTypeHero) {
        float uncapped = static_cast<float>(size);
        size = uncapped <= kHeroSizeCap ? uncapped : kHeroSizeCap;
    }
    combat.sizeClass = SizeClassOf(size, load);
    combat.sizeClass20 = SizeClassOf(size * kSizeScale20, load);
    combat.sizeClass60 = SizeClassOf(size * kSizeSmall, load);
}

void CUnitServer::UpdateMoveFactors() {
    const CWorldServer* world = ServerWorld();
    double factor = world->nightLevel_;
    double scaled = (factor * stats.perception[1].current) * kWorldFactorScale;
    moveFactors[0] = 1.0f;
    moveFactors[2] = 1.0f;
    moveFactors[1] = 1.0f;
    double sum = scaled + (1.0f - factor);
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
        moveFactors[0] = 0.0f;
    }
    if (state == 5 && (action.kind == kOrderCastAt || action.kind == kOrderCastOn)) {
        CSpell* spell = SpellOfSource(action.source);
        if (spell && spell->GetPrototype()->castType != 1)
            moveFactors[2] = kCastingFactor2 * moveFactors[2];
    }
    if (state != 0) {
        speedClass = 15;
        return;
    }
    float speed = static_cast<float>(unit->MoveSpeed());
    if (speed > kSpeedFast) speedClass = 0;
    else if (speed > kSizeSmall) speedClass = 1;
    else if (speed > kPointThree) speedClass = 3;
    else if (speed > kSpeedSlow) speedClass = 7;
    else speedClass = 15;
}
