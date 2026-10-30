// Combat: UnitServer's weapon and spell queries, the attack hit (damage calculation), the attack
// roll and hit resolution; the controller's lifetime, its reactions to other objects and its
// statistics. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "ai_map.h"
#include "combat_internal.h"
#include "combat_plan.h"
#include "console.h"
#include "core_refcount.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "logic_settings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "object_diplomacy.h"
#include "object_tables.h"
#include "options.h"
#include "player_messages.h"
#include "random.h"
#include "sector_map.h"
#include "spell.h"
#include "ui.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <list>
#include <string>

// The original's float maths ran at the main thread's 24-bit precision: float maths in its order here, and an int
// or a double operand through lib/pc24.h's one rounding.

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

float DifficultyAttackFactor() {
    int difficulty = options::g_options.difficulty;
    return g_logicSettings.attack[difficulty];
}

float ServerMana(const Unit* unit) { return unit->ServerCompanion()->vitals.mana; }

std::uint8_t PlayerGodMode(const Player* player) { return player->godMode; }

void AttackParamsOf(UnitServer* controller, AttackParams* out) { controller->GetAttackParams(out); }

float SpellDamage(const Spell* spell) { return spell->damage; }
float SpellRange(const Spell* spell) { return spell->range; }
float SpellCost(const Spell* spell) { return spell->manaCost; }

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
std::int16_t& LastDamageSource(Unit* unit) { return unit->params.lastHitSource; }

// One size class of the size computation: by the load first, then by `size`.
std::int16_t SizeClassOf(float size, float load) {
    if (load > kLoadOverloaded) return 7;
    if (load > kLoadHeavy) return size > kSizeHeavy ? 6 : 5;
    if (size >= kSizeLarge) return 4;
    if (size >= kSizeMedium) return 3;
    return size >= kSizeSmall ? 2 : 1;   // also for a NaN size
}

} // namespace



float UnitServer::AttackRange() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (weapon) return weapon->reach;
    return stats.combat[UnitStats::kReach];
}

bool UnitServer::HasRangedWeapon() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (!weapon) return false;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow;
}

bool UnitServer::AttacksFromRange() {
    if (!unit->ActiveWeapon()) {
        return stats.naturalWeapon != kNoNaturalWeapon && stats.combat[UnitStats::kReach] > 1.0f;
    }
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (!weapon) return false;
    return weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow;
}

void UnitServer::GetDamageByType(DamageWords* out) {
    Unit* self = unit;
    ItemWeapon* weapon;
    if (self->player && self->activeWeapon < 4 &&
        (weapon = AsWeapon(self->weapons[self->activeWeapon])) != nullptr) {
        float total = weapon->damageSpread + weapon->damage;
        float scale = ScaleDamageByEffects(self, total);   // (not read)
        float weights[7];
        weapon->GetDamageTypes(weights);
        for (int i = 0; i < 7; ++i) {
            float value = scale * weights[i];
            out->words[1 + i] = static_cast<std::uint16_t>(lib::RoundEven(value));
        }
        return;
    }
    float total = stats.damageSpread + stats.damage;
    float scale = ScaleDamageByEffects(self, total);
    for (int i = 0; i < 7; ++i) {
        float value = scale * stats.damageWeights[i];
        out->words[1 + i] = static_cast<std::uint16_t>(lib::RoundEven(value));
    }
}

void UnitServer::GetSpellDamage(std::uint32_t source, DamageWords* out) {
    Spell* spell = SpellOfSource(source);
    *out = DamageWords{};
    if (spell) {
        auto value = static_cast<std::uint16_t>(lib::Truncate(SpellDamage(spell)));
        int damageType = spell->GetDamageType();
        out->words[1 + damageType] = value;
    }
    out->words[0] = 0x7FFF;
}

Item* UnitServer::SpellCarrier(std::uint32_t source) {
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item) return nullptr;
    if (item->GetClassId() != ClassId::ItemQuick) return nullptr;
    if (item->GetCategory() == QuickItemCategory::Wand || item->GetCategory() == QuickItemCategory::Potion) return item;
    return nullptr;
}

bool UnitServer::IsScrollSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return false;
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item || item->GetClassId() != ClassId::ItemQuick) return false;
    if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return false;
    return item->GetCategory() == QuickItemCategory::Potion;
}

Spell* UnitServer::SpellOfSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return unit->GetSpellObject(source);
    auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
    if (!item || item->GetClassId() != ClassId::ItemQuick) return nullptr;
    if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return nullptr;
    return item->spell;
}

bool UnitServer::IsFighting() {
    if (action.kind == UnitOrderKind::CastAt || action.kind == UnitOrderKind::CastOn) {
        Spell* spell = nullptr;
        if (action.source & kQuickItemSource) {
            auto* item = unit->GetQuickItem(action.source & ~kQuickItemSource);
            if (item && item->GetClassId() == ClassId::ItemQuick && (item->GetCategory() == QuickItemCategory::Wand || item->GetCategory() == QuickItemCategory::Potion))
                spell = item->spell;
        } else {
            spell = unit->GetSpellObject(action.source);
        }
        if (spell) {
            SpellType spellType = spell->Type();
            if (spellType <= SpellType::AcidFog) return true;
            // Antimagic is not a hostile cast (Rebirth's FixPlaingBattleMusicFromAntimagic; the original counted it).
        }
    }
    return action.kind == UnitOrderKind::Attack;
}

bool UnitServer::CanCast(const AIOrder* order) {
    std::uint32_t source = order->source;
    if (source & kQuickItemSource) {
        auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
        if (!item || item->GetClassId() != ClassId::ItemQuick) return false;
        if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return false;
        Spell* spell = item->spell;
        if (!spell) return false;
        if (item->GetCategory() != QuickItemCategory::Wand) return true;
        // The charge against the cost: less (or unordered) refuses.
        return item->charge >= SpellCost(spell);
    }
    Spell* spell = unit->GetSpellObject(source);
    if (!spell) return false;
    if (unit->player != nullptr) {
        float cost = SpellCost(spell);
        float mana = ServerMana(unit);
        float needed = stats.Period20() * cost;
        if (needed > mana) return false;
    }
    return true;
}

std::optional<Vector3> UnitServer::GetMovePoint() {
    if (action.kind == UnitOrderKind::Move) return action.point;
    if (action2.kind == UnitOrderKind::Move) return action2.point;
    return std::nullopt;
}

float UnitServer::ActionDistance(const AIOrder* order) {
    if (order->pointIsUseSpot) return kPointTwo;
    UnitOrderKind kind = order->kind;
    MapObject* target = order->target;
    if (kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn) {
        if (!unit->player && stats.naturalWeapon == kNoNaturalWeapon) return stats.combat[UnitStats::kReach];
        std::uint32_t source = order->source;
        Spell* spell;
        if (source & kQuickItemSource) {
            Item* carrier = SpellCarrier(source);
            if (!carrier) return kTen;
            spell = carrier->spell;
        } else {
            spell = unit->GetSpellObject(source);
        }
        if (spell) return SpellRange(spell);
        return kTen;
    }
    if (kind == UnitOrderKind::Use) {
        if (order->Skill() == UseSource::SearchBody) {
            float part = target->Radius() * kPointThree;
            return unit->Radius() + part;
        }
        if (Unit* other = UnitOrNull(target)) {
            return (unit->standingHeight + other->standingHeight) + kPointSix;
        }
        float own = unit->Radius();
        return (target->Radius() + own) - kPointOne;
    }
    if (Unit* other = UnitOrNull(target)) {
        float floor = kMeleeFloor;
        if (other->player && unit->player) floor = kMeleeFloorParty;
        ItemWeapon* weapon = unit->ActiveWeapon();
        float range = weapon ? weapon->reach : stats.combat[UnitStats::kReach];
        float reach = MaxKeepB(range, floor);
        return (unit->standingHeight + other->standingHeight) + reach;
    }
    ItemWeapon* weapon = unit->ActiveWeapon();
    float range = weapon ? weapon->reach : stats.combat[UnitStats::kReach];
    float own = unit->Radius();
    return (target->Radius() + own) + range;
}

bool UnitServer::CanReachTarget(const AIOrder* order) {
    if (order->kind != UnitOrderKind::Attack && order->kind != UnitOrderKind::Use) return true;
    Unit* other = UnitOrNull(order->target);
    if (!other) return true;
    if (other->IsDead() != 0) return true;
    if (other->sizeClass != 0) return true;
    if (order->kind == UnitOrderKind::Attack) {
        ItemWeapon* weapon = unit->ActiveWeapon();
        if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return true;
    }
    float offset = other->GetPrototype()->height;
    float height = offset + other->localPosition.z;
    EI_NAN_AUDIT(height);
    return height < kOnePointFive;
}

std::uint32_t UnitServer::WeaponTypeMask() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (weapon) return AnimationWeaponBit(weapon->GetType());
    int natural = stats.naturalWeapon;
    if (natural < 7) return AnimationWeaponBit(natural);
    return 0;
}

std::uint32_t UnitServer::MoveModeMask(const MoveModeState* mode) {
    switch (static_cast<std::uint32_t>(mode->mode)) {
    case 0: return AnimationKeyOf(AnimationMoveBits::Mode0);
    case 1: return AnimationKeyOf(AnimationMoveBits::Mode1);
    case 2:
        if (mode->onGuard || unit->MapType() != MapObjectType::Hero) return AnimationKeyOf(AnimationMoveBits::Mode2);
        return 0;
    case 4: return AnimationKeyOf(AnimationMoveBits::Mode4);
    default: return 0;
    }
}

int UnitServer::Level() {
    float experience = stats.experience;
    float base = g_logicSettings.considerModifier;
    float step = g_logicSettings.considerLogBase;
    // ln(experience / base) / ln(step): FYL2X's two 64-bit logarithms, one rounding; then to the nearest integer.
    const float ratio = pc24::Div(pc24::Log(experience / base), pc24::Log(step));
    return lib::RoundEven(ratio);
}

void UnitServer::EffectsChanged() {
    if (unit->HasEffect(SpellType::Feeblemind) && stats.naturalWeapon != kNoNaturalWeapon) unit->flags |= MapObjectFlag::NoCasting;
    else unit->flags &= ~MapObjectFlag::NoCasting;
}

void UnitServer::GetAttackParams(AttackParams* out) {
    Unit* self = unit;
    int keyed = IsTextKey(self->id);
    ItemWeapon* weapon = self->ActiveWeapon();
    if (keyed && weapon) {
        out->damageMin = weapon->damage;
        out->damageMax = weapon->damageSpread + weapon->damage;
        out->weaponType = weapon->GetType();
        out->damageType = WeaponDamageType(weapon->prototypeId);
        out->speed = weapon->weight;
        float base = WeaponAttackBonus(weapon->prototypeId);
        float bonus = stats.WeaponSkillBonus(out->weaponType, self);
        out->attack = lib::Truncate(bonus + base);
        base = WeaponDefenceBonus(weapon->prototypeId);
        bonus = stats.WeaponSkillBonus(out->weaponType, self);
        out->defence = lib::Truncate(bonus + base);
        out->damageType = WeaponDamageType(weapon->prototypeId);
    } else {
        out->damageMin = stats.damage;
        out->damageMax = stats.damageSpread + stats.damage;
        out->attack = lib::Truncate(stats.combat[UnitStats::kAttack]);
        out->defence = lib::Truncate(stats.combat[UnitStats::kDefence]);
        out->speed = stats.combat[UnitStats::kSpeed];
        if (weapon) {
            out->weaponType = weapon->GetType();
            out->damageType = WeaponDamageType(weapon->prototypeId);
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

float UnitServer::AttackRoll(UnitServer* attacker, bool backstab) {
    AttackParams theirs;
    AttackParams mine;
    AttackParamsOf(attacker, &theirs);
    AttackParamsOf(this, &mine);
    float speedRatio = theirs.speed / (kSpeedScale * mine.speed);
    const Unit* self = unit;
    const Unit* other = attacker->unit;
    float dx = self->localPosition.x - other->localPosition.x;
    float dy = self->localPosition.y - other->localPosition.y;
    float dz = self->localPosition.z - other->localPosition.z;
    float distance = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
    float twice = distance + distance;
    float radius = unit->Radius() * kDistanceScale;
    float penalty = twice / (radius + radius);

    float roll = 0.0f;
    float spread = theirs.attackSpread;
    if (spread > 0.0f) {
        int sides = lib::RoundEven(spread);
        std::uint32_t random = g_random.Next();
        // The draw doubled rounds once: as the draw rounded to a float, doubled.
        float drawn = static_cast<float>(static_cast<double>(random % static_cast<std::uint32_t>(sides + 1)));
        roll = (drawn + drawn) - (spread + 1.0f);
    }
    roll = pc24::Add(roll, static_cast<double>(theirs.attack)) - penalty;   // the int exact, one rounding
    if (!backstab) roll = roll + speedRatio;
    return MaxKeepB(0.0f, roll);
}

bool UnitServer::ResolveHit(DamageInfo* hit, Unit* attacker) {
    float roll = kHitMark;
    if (!HasElementalDamage(hit)) {
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
    return result > 0.0f;
}

void UnitServer::BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot) {
    Unit* self = unit;
    int keyed = IsTextKey(self->id);
    std::memset(out, 0, sizeof(DamageInfo));
    out->multiplier = 1.0f;
    ItemWeapon* weapon = nullptr;
    if (keyed && self->activeWeapon < 4) weapon = AsWeapon(self->weapons[self->activeWeapon]);
    if (weapon) {
        weapon->GetDamageTypes(out->typeWeights);
        out->weaponValue = weapon->actionValue;
        int sides = static_cast<int>(kFive);
        int drawn = libcrt::Rand();
        int face = ((drawn * sides) / 0x7FFF) % sides;
        int value = face + face + 2;
        float roll = static_cast<float>(value) - (kFive + 1.0f);
        out->attackRoll = roll;
        out->amount = static_cast<float>(weapon->RollDamage());   // stored as a float, as the original
    } else {
        std::memcpy(out->typeWeights, stats.damageWeights, sizeof(out->typeWeights));
        out->weaponValue = 0;
        float factor = keyed ? 1.0f : DifficultyAttackFactor();
        float roll = factor * stats.combat[UnitStats::kAttack];
        out->attackRoll = roll;
        float extra = 0.0f;
        float spread = stats.damageSpread;
        EI_NAN_AUDIT(spread);
        if (spread != 0.0f) {
            float scaled = spread * kHundred;
            int sides = lib::RoundEven(scaled);
            std::uint32_t random = g_random.Next();
            extra = pc24::Mul(static_cast<double>(random % static_cast<std::uint32_t>(sides + 1)), kPercent);
        }
        out->amount = extra + stats.damage;
    }
    out->amount = ScaleDamageByEffects(self, out->amount);
    out->location = location;
    out->hitKind = 0;
    if (backstab) out->backstabFactor = stats.BackstabFactor();
    else out->backstabFactor = 1.0f;
    if (self->player && PlayerGodMode(self->player) >= 2) ResetDamageInfo(out);
    out->calledShot = static_cast<std::uint8_t>(calledShot);
}

bool UnitServer::TakeHit(DamageInfo* hit, Unit* attacker, Unit* source) {
    WorldServer* world = ServerWorld();
    int dead = unit->IsDead();
    bool hadPlayer = unit->player != nullptr;
    // |attackRoll| below FLT_MAX: not resolved yet.
    float roll = std::bit_cast<float>(std::bit_cast<std::uint32_t>(hit->attackRoll) & 0x7FFFFFFFu);
    EI_NAN_AUDIT(roll);
    if (roll < kHitMark) ResolveHit(hit, attacker);
    if (!dead) {
        world->AddToUpdateMap(unit);
        if (attacker) {
            kCombatLog << "Unit " << static_cast<long>(unit->id) << " was attacked by " << static_cast<long>(attacker->id);
        } else {
            kCombatLog << "Unit " << static_cast<long>(unit->id) << " was attacked by xz";
        }
        kCombatLog << " skill=" << hit->attackRoll;
        float amount = hit->amount;
        kCombatLog << " damage=" << amount;
        if (hit->backstabFactor > 1.0f) {
            float power = hit->backstabFactor;
            kCombatLog << " backstab with power=" << power;
        }
        if (hit->calledShot) kCombatLog << " called shot";
        console::EndLine(kCombatLog << " destHL=" << HitLocationName(hit->location));
    }

    Player* player = unit->player;
    if (player) {
        if (PlayerGodMode(player) != 0) return false;
        if (source && source->player && player != source->player) return false;
    }

    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > 1.0f) mine.defence = 0;
    EI_NAN_AUDIT(hit->attackRoll);
    if (static_cast<double>(mine.defence) <= hit->attackRoll) {
        stats.flags |= UnitStatsFlag::Struck;
        unit->MarkChanged(4);
        TriggerItemSpells(1);
        Item* weapon = attacker ? attacker->GetWeapon(attacker->activeWeapon) : nullptr;
        if (stats.ApplyHit(unit, ServerStatsOf(unit), hit, weapon)) {
            if (attacker && !HasElementalDamage(hit)) {
                if (attacker->MapType() != MapObjectType::Hero) {
                    LastDamageSource(unit) = static_cast<std::int16_t>(attacker->prototypeId + 0x10);
                } else {
                    auto* controller = attacker->ServerController();
                    ItemWeapon* own = controller->unit->ActiveWeapon();
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
                if (unit->IsDead() != 0 && source && source->player && !hadPlayer) {
                    const auto* prototype = unit->GetPrototype();
                    if (IsMultiplayerClient()) {
                        world->AwardExperience(nullptr, prototype->experienceValue);
                        return true;
                    }
                    Player* killer = source->player;
                    world->AwardExperience(killer, prototype->experienceValue);
                }
            }
            return true;
        }
    }
    if (attacker && attacker->GetClassId() == ClassId::Unit) {
        auto* diplomacy = &ServerWorld()->diplomacy;
        diplomacy->OnAttack(attacker, unit);
    }
    brain.SetSeen(attacker);
    return false;
}

void UnitServer::UpdateSizeClasses() {
    UnitStats& combat = stats;
    if (combat.movementKind == 2) {
        combat.sizeClass20 = 0;
        combat.sizeClass = 0;
        return;
    }
    float load = unit->BodyExtent();
    const auto* model = unit->GetPrototypeModel();
    float size = unit->DoubleHeight() * model->sizeScale;
    if (unit->MapType() == MapObjectType::Hero) size = MinKeepA(kHeroSizeCap, size);
    combat.sizeClass = SizeClassOf(size, load);
    combat.sizeClass20 = SizeClassOf(size * kSizeScale20, load);
    combat.sizeClass60 = SizeClassOf(size * kSizeSmall, load);
}

void UnitServer::UpdateMoveFactors() {
    const WorldServer* world = ServerWorld();
    float factor = world->nightLevel;
    float scaled = (factor * stats.perception[1].current) * kWorldFactorScale;
    moveFactors[0] = 1.0f;
    moveFactors[2] = 1.0f;
    moveFactors[1] = 1.0f;
    float sum = scaled + (1.0f - factor);
    moveFactors[3] = sum;
    moveFactors[3] = std::bit_cast<float>(world->weatherFactor) * sum;
    if (unit->moveMode == 0) {
        moveFactors[2] = kMode0Factor2;
        moveFactors[3] = kMode0Factor3 * moveFactors[3];
    } else if (unit->moveMode == 1) {
        moveFactors[2] = kMode1Factor2;
    }
    if (state == UnitState::Idle) {
        if (unit->moveMode == 0) moveFactors[0] = kIdleMode0Factor0;
        else if (unit->moveMode == 1) moveFactors[0] = kIdleMode1Factor0;
        if (unit->moveMode == 3) {
            moveFactors[0] = kIdleMode3Scale * moveFactors[0];
            moveFactors[1] = kIdleMode3Factor1;
        }
    } else {
        moveFactors[0] = 0.0f;
    }
    if (state == UnitState::Acting && (action.kind == UnitOrderKind::CastAt || action.kind == UnitOrderKind::CastOn)) {
        Spell* spell = SpellOfSource(action.source);
        if (spell && spell->GetPrototype()->castType != 1)
            moveFactors[2] = kCastingFactor2 * moveFactors[2];
    }
    if (state != UnitState::Idle) {
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

namespace {
const char* const kServerName = "CUnitServer";
constexpr float kNoRange = 1e38f;
static_assert(std::bit_cast<std::uint32_t>(kNoRange) == 0x7E967699u);
} // namespace

// --- UnitServer's lifetime, class name and id -----------------------------------------------------

UnitServer::UnitServer() { ResetController(); }

// ResetController empties the brain (Init) and releases every reference the controller holds.
UnitServer::~UnitServer() { ResetController(); }

const char* UnitServer::ClassName() { return kServerName; }
ClassId UnitServer::GetClassId() { return ClassId::UnitServer; }

void UnitServer::MoveFactorsChanged() { UpdateMoveFactors(); }

// --- OrderPlan ----------------------------------------------------------------------------------

void OrderPlan::TakeSteps(AnimationSteps steps, int start, int duration) {
    targetId = -1;
    startTime = start;
    hitLocation = 1;
    rangeSq = kNoRange;
    isCast = 0;
    notSkill = 0;
    castKind = 8;
    endTime = start + duration;
    animation = std::move(steps);
}

// --- Other objects and the unit's own setup: hostility claims, taking up a weapon, an object being deleted,
// settling the unit again.

namespace {
constexpr int kCallClaim = 0x1E;        // the kind of the AI memory's call for a claim
constexpr int kPartWeapons = 6;         // Unit::MarkChanged part


// The weapon's condition (Item +0x20, a float) rounded to nearest is above 0.
bool Usable(const Item* weapon) { return weapon && lib::RoundEven(weapon->condition) > 0; }
} // namespace

void UnitServer::ClaimHostility(Unit* claimant, std::uint32_t mask) {
    claimedHostile |= mask;
    std::string text;
    for (int player = 0; player < 32; ++player) {
        if (!(mask & (1u << player))) continue;
        std::string piece;
        piece = std::format("Unit {} claims that player {} are enemies! ", claimant->id, player);
        text += piece;
    }
    brain.memory.AddClaim(mask, kCallClaim, text);
}

bool UnitServer::TakeUpWeapon(int slot) {
    auto* weapon = unit->GetWeapon(static_cast<std::uint32_t>(slot));
    int taken = Usable(weapon) ? slot : -1;
    unit->MarkChanged(kPartWeapons);
    unit->activeWeapon = static_cast<std::uint32_t>(taken);
    unit->pendingWeapon = taken;
    if (unit->player) brain.attention.Build(unit);
    RefreshItemSpells();
    return Usable(weapon);
}

void UnitServer::OnObjectDeleted(EngineObject* object) {
    if (!object) return;
    if (action.target == object) {
        action.Reset();
        if (action2.kind == UnitOrderKind::None) Idle();
    }
    if (action2.target == object && action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote)
        DropQueuedOrder();
    if (currentPlan.targetId == object->id) EndOrder(0);
    brain.OnObjectDeleted(object);
    if (followedLeader == object) {
        followsLeader = 0;
        followedLeader.Reset();
    }
}

void UnitServer::Resettle() {
    if (unit->IsDead()) return;
    auto* world = ServerWorld();
    world->aiMap.sectorMap.Remove(unit);
    unit->AsObject3D()->SetUpAnimatedFigure(unit->name);
    unit->UpdateSizeClasses();
    unit->UpdateSizeClass();
    AIMap* map = &world->aiMap;
    if (!map->CanLeaveCell(unit)) {
        auto* position = &unit->localPosition;
        Vector3 spot;
        spot = *position;
        if (map->FindFreeCellNear(unit, *position, &spot)) {
            *position = spot;
        } else {
            DamageInfo hit;
            ResetDamageInfo(&hit);
            TakeHit(&hit, nullptr, nullptr);
            g_serverWorld.aiMap.sectorMap.Add(unit);
            return;
        }
    }
    g_serverWorld.aiMap.sectorMap.Add(unit);
}

void UnitServer::Create() {
    ResetController();
    brain.SetOwner(this);
}

void UnitServer::ClearState() { ResetController(); }

void UnitServer::Destroy() { ResetController(); }

int UnitServer::TamingStage() { return tameStage; }

// --- Statistics: the vitals and the protection recomputed, the statistics repacked, the armour layers, the
// equipment's AI options, experience.

namespace {
// The defence factor of units without a player, per difficulty (options::g_options.difficulty).
inline const float* DefenceFactors() { return g_logicSettings.defence; }

UnitVitals& VitalsOf(Unit* unit) { return unit->ServerCompanion()->vitals; }


} // namespace

void UnitServer::RefreshVitals(bool withHitPoints) {
    int now = WorldContext()->tick;
    float oldRate = stats.rate;
    if (withHitPoints) stats.RecomputeHitPoints(&VitalsOf(unit), unit);
    stats.Recompute(&VitalsOf(unit), unit);
    if (nextAttackTime > now) {
        // The int exact, one rounding.
        float scaled = pc24::Mul(static_cast<double>(nextAttackTime - now), oldRate) / stats.rate;
        nextAttackTime = lib::RoundEven(scaled) + now;
    }
    unit->MarkChanged(0);
}

void UnitServer::UpdateProtection() {
    stats.PackBodyProtection(unit, &VitalsOf(unit), &protection);
    float factor = unit->player ? 1.0f : DefenceFactors()[options::g_options.difficulty];
    ItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
    if (unit->player && weapon) {
        float bonus = stats.WeaponSkillBonus(weapon->GetType(), unit);
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(bonus * factor));
    } else {
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(factor * stats.combat[UnitStats::kDefence]));
    }
}

void UnitServer::RepackStats() {
    std::uint8_t speedsBefore[sizeof(unit->params.modeSpeeds)];
    std::memcpy(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore));
    stats.Pack(&unit->params);
    stats.flags = 0;
    UpdateProtection();
    unit->MarkChanged(4);
    if (std::memcmp(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore)) != 0) motionChanged = 1;
}

bool UnitServer::ArmourPutOn(std::uint32_t key) {
    // A key out of range gives no record; the original then faults reading its type, as this does.
    auto* record = itemdb::ArmorAt(key >> 16);
    stats.AddArmourLayer(unit->armor[record->type]);
    return false;
}

bool UnitServer::RebuildArmourLayers() {
    stats.RebuildArmourLayers(unit);
    return true;
}

int UnitServer::ClearOuterArmourLayers() { return stats.ClearOuterLayers(); }

void UnitServer::RefreshEquipment() {
    RefreshItemSpells();
    brain.attention.Build(unit);
}

void UnitServer::HostileMaskChanged() {
    claimedHostile = 0;
    brain.memory.calls.RemoveClaims();
}

void UnitServer::CopyStats(UnitStats* out) { std::memcpy(out, &stats, sizeof(UnitStats)); }

void UnitServer::AddExperience(float amount) {
    stats.GainExperience(unit, amount);
    RefreshVitals(false);
}
