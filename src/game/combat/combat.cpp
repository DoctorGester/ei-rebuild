// Combat: UnitServer's weapon and spell queries, the attack hit (damage calculation), the attack
// roll and hit resolution; the controller's lifetime, its reactions to other objects and its
// statistics. See combat.h and docs/notes/combat.md.
#include "game/combat/combat.h"

#include "game/ai/ai_map.h"
#include "game/combat/combat_internal.h"
#include "game/console/console.h"
#include "game/core/core_refcount.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "game/app/logic_settings.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/world/diplomacy.h"
#include "game/data/text_manager.h"
#include "game/app/options.h"
#include "game/players/player.h"
#include "game/core/random.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <list>
#include <string>

namespace {
constexpr float kPointOne = 0.1f;
constexpr float kPointTwo = 0.2f;
constexpr float kPointThree = 0.3f;
constexpr float kPointSix = 0.6f;
constexpr std::uint32_t kRollSides = 5;           // a weapon hit rolls one of five faces
constexpr float kDefaultCastReach = 10.0f;
constexpr float kCalledShotLimb = 10.0f;
constexpr float kDistanceScale = 10.0f;
constexpr float kFifteen = 15.0f;
constexpr float kSpeedScale = 20.0f;
constexpr float kCalledShotHead = 25.0f;
constexpr float kSpreadSteps = 100.0f;
constexpr float kPercent = 0.01f;
constexpr float kHitMark = 3.4028235e38f;       // FLT_MAX
constexpr float kMissMark = -3.4028235e38f;      // -FLT_MAX
constexpr float kMeleeFloor = 0.6f;

constexpr float kHeroSizeCap = 1.89f;
constexpr float kLoadOverloaded = 1.5f;
constexpr float kLoadHeavy = 0.85f;
constexpr float kSizeHeavy = 1.7f;
constexpr float kSizeSmall = kPointSix;
constexpr float kSizeMedium = 1.2f;
constexpr float kSizeLarge = 1.9f;
constexpr float kSizeScale20 = kPointTwo;                           // 0.2
constexpr float kWorldFactorScale = kPercent;                       // 0.01
constexpr float kMode0Factor2 = 0.5f;
constexpr float kMode1Factor2 = 0.75f;
constexpr float kIdleMode0Factor0 = 0.4f;
constexpr float kIdleMode1Factor0 = kPointTwo;                      // 0.2
constexpr float kIdleMode3Scale = 2.0f;
constexpr float kIdleMode3Factor1 = 0.5f;
constexpr float kCastingFactor2 = 1.5f;
constexpr float kSpeedFast = 1.2f;
constexpr float kSpeedMedium = kPointSix;                           // 0.6
constexpr float kSpeedSlow = 0.15f;


float DifficultyAttackFactor() {
    int difficulty = options::g_options.difficulty;
    return g_logicSettings.attack[difficulty];
}

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
    return stats.combat[UnitStats::CombatIndex::Reach];
}

bool UnitServer::HasRangedWeapon() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    return weapon && weapon->IsRanged();
}

bool UnitServer::AttacksFromRange() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (!weapon) return stats.naturalWeapon != WeaponType::SpellsOnly && stats.combat[UnitStats::CombatIndex::Reach] > 1.0f;
    return weapon->IsRanged();
}

void UnitServer::GetDamageByType(DamageWords* out) {
    Unit* self = unit;
    ItemWeapon* weapon = nullptr;
    if (self->player && self->activeWeapon < 4 && self->weapons[self->activeWeapon])
        weapon = self->weapons[self->activeWeapon]->AsWeapon();
    // A player's weapon in hand gives the damage and its type weights; else the statistics do.
    std::array<float, kDamageTypeCount> weaponWeights{};
    const float* weights = stats.damageWeights;
    float total = stats.damageSpread + stats.damage;
    if (weapon) {
        total = weapon->damageSpread + weapon->damage;
        weaponWeights = weapon->DamageTypes();
        weights = weaponWeights.data();
    }
    float scale = ScaleDamageByEffects(self, total);
    for (int i = 0; i < 7; ++i) {
        float value = scale * weights[i];
        out->words[1 + i] = static_cast<std::uint16_t>(lib::RoundEven(value));
    }
}

void UnitServer::GetSpellDamage(std::uint32_t source, DamageWords* out) {
    Spell* spell = SpellOfSource(source);
    *out = DamageWords{};
    if (spell) {
        auto value = static_cast<std::uint16_t>(lib::Truncate(SpellDamage(spell)));
        out->words[1 + static_cast<int>(spell->Use().damageType)] = value;
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

bool UnitServer::IsPotionSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return false;
    Item* item = SpellCarrier(source);
    return item && item->GetCategory() == QuickItemCategory::Potion;
}

Spell* UnitServer::SpellOfSource(std::uint32_t source) {
    if (!(source & kQuickItemSource)) return unit->GetSpellObject(source);
    Item* carrier = SpellCarrier(source);
    if (!carrier) return nullptr;
    return carrier->spell;
}

bool UnitServer::IsFighting() {
    if (action.kind == UnitOrderKind::CastAt || action.kind == UnitOrderKind::CastOn) {
        Spell* spell = SpellOfSource(action.source);
        if (spell && spell->Use().fighting) return true;
    }
    return action.kind == UnitOrderKind::Attack;
}

bool UnitServer::CanCast(const AIOrder* order) {
    std::uint32_t source = order->source;
    if (source & kQuickItemSource) {
        Item* item = SpellCarrier(source);
        if (!item || !item->spell) return false;
        if (item->GetCategory() != QuickItemCategory::Wand) return true;
        // The charge against the cost: less (or unordered) refuses.
        return item->charge >= SpellCost(item->spell);
    }
    Spell* spell = unit->GetSpellObject(source);
    if (!spell) return false;
    if (unit->player != nullptr) {
        float cost = SpellCost(spell);
        float mana = unit->Vitals().mana;
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
        if (!unit->player && stats.naturalWeapon == WeaponType::SpellsOnly) return stats.combat[UnitStats::CombatIndex::Reach];
        Spell* spell = SpellOfSource(order->source);
        if (spell) return SpellRange(spell);
        return kDefaultCastReach;
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
        if (other->player && unit->player) floor = kPointTwo;
        ItemWeapon* weapon = unit->ActiveWeapon();
        float range = weapon ? weapon->reach : stats.combat[UnitStats::CombatIndex::Reach];
        float reach = MaxKeepB(range, floor);
        return (unit->standingHeight + other->standingHeight) + reach;
    }
    ItemWeapon* weapon = unit->ActiveWeapon();
    float range = weapon ? weapon->reach : stats.combat[UnitStats::CombatIndex::Reach];
    float own = unit->Radius();
    return (target->Radius() + own) + range;
}

bool UnitServer::TooHighToReach(const AIOrder* order) {
    if (order->kind != UnitOrderKind::Attack && order->kind != UnitOrderKind::Use) return false;
    Unit* other = UnitOrNull(order->target);
    if (!other || other->IsDead()) return false;
    if (other->sizeClass != 0) return false;
    if (order->kind == UnitOrderKind::Attack && HasRangedWeapon()) return false;
    float height = other->GetPrototype()->height + other->localPosition.z;
    return height >= kMeleeReachHeight;
}

std::uint32_t UnitServer::WeaponTypeMask() {
    ItemWeapon* weapon = unit->ActiveWeapon();
    if (weapon) return AnimationWeaponBit(weapon->GetType());
    if (static_cast<int>(stats.naturalWeapon) < kAnimatedWeaponTypes) return AnimationWeaponBit(stats.naturalWeapon);
    return 0;
}

std::uint32_t UnitServer::MoveModeMask(const MoveModeState* mode) {
    switch (mode->mode) {
    case MoveMode::Lie: return AnimationKeyOf(AnimationMoveBits::Lie);
    case MoveMode::Crawl: return AnimationKeyOf(AnimationMoveBits::Crawl);
    case MoveMode::Walk:
        if (mode->onGuard || unit->MapType() != MapObjectType::Hero) return AnimationKeyOf(AnimationMoveBits::Walk);
        return 0;
    case MoveMode::Stop: return AnimationKeyOf(AnimationMoveBits::Stop);
    default: return 0;
    }
}

int UnitServer::Level() {
    float experience = stats.experience;
    float base = g_logicSettings.considerModifier;
    float step = g_logicSettings.considerLogBase;
    // ln(experience / base) / ln(step), then to the nearest integer.
    const float ratio = lib::Log(experience / base) / lib::Log(step);
    return lib::RoundEven(ratio);
}

void UnitServer::EffectsChanged() {
    if (unit->EffectTotals()[EffectStat::NoCasting] > 0.0f && stats.naturalWeapon != WeaponType::SpellsOnly)
        unit->flags |= MapObjectFlag::NoCasting;
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
    } else {
        out->damageMin = stats.damage;
        out->damageMax = stats.damageSpread + stats.damage;
        out->attack = lib::Truncate(stats.combat[UnitStats::CombatIndex::Attack]);
        out->defence = lib::Truncate(stats.combat[UnitStats::CombatIndex::Defence]);
        out->speed = stats.combat[UnitStats::CombatIndex::Speed];
        if (weapon) {
            out->weaponType = weapon->GetType();
            out->damageType = WeaponDamageType(weapon->prototypeId);
        } else {
            out->weaponType = stats.naturalWeapon;
            const auto* model = self->GetPrototypeModel();
            // The first damage type with a non-zero weight, none when every weight is zero.
            out->damageType = kNoDamageType;
            for (int i = 0; i < 7; ++i) {
                float weight = model->damageWeights[i];
                if (weight != 0.0f) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (IsRanged(out->weaponType)) out->defence = 0;
    out->damageMax = ScaleDamageByEffects(self, out->damageMax);
    out->damageMin = ScaleDamageByEffects(self, out->damageMin);
    out->attackSpread = stats.combat[UnitStats::CombatIndex::AttackSpread];
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
    float distance = std::sqrt(LengthSquared(self->localPosition - other->localPosition));
    float twice = distance + distance;
    float radius = unit->Radius() * kDistanceScale;
    float penalty = twice / (radius + radius);

    float roll = 0.0f;
    float spread = theirs.attackSpread;
    if (spread > 0.0f) {
        int sides = lib::RoundEven(spread);
        std::uint32_t random = g_random.Next();
        float drawn = static_cast<float>(random % static_cast<std::uint32_t>(sides + 1));
        roll = (drawn + drawn) - (spread + 1.0f);
    }
    roll = (roll + static_cast<float>(theirs.attack)) - penalty;
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
            else if (hit->location != 1) roll = MaxKeepB(0.0f, roll - kCalledShotLimb);
        }
    }
    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > 1.0f) mine.defence = 0;
    float result = static_cast<float>(mine.defence) > roll ? kMissMark : kHitMark;
    hit->attackRoll = result;
    return result > 0.0f;
}

void UnitServer::BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot) {
    Unit* self = unit;
    int keyed = IsTextKey(self->id);
    std::memset(out, 0, sizeof(DamageInfo));
    out->multiplier = 1.0f;
    ItemWeapon* weapon = nullptr;
    if (keyed && self->activeWeapon < 4 && self->weapons[self->activeWeapon])
        weapon = self->weapons[self->activeWeapon]->AsWeapon();
    if (weapon) {
        std::ranges::copy(weapon->DamageTypes(), out->typeWeights);
        out->weaponValue = weapon->actionValue;
        int face = static_cast<int>(g_random.Next() % kRollSides);
        out->attackRoll = static_cast<float>(2 * face - 4);   // -4 .. 4 in steps of 2
        out->amount = weapon->RollDamage();   // stored as a float, as the original
    } else {
        std::memcpy(out->typeWeights, stats.damageWeights, sizeof(out->typeWeights));
        out->weaponValue = 0;
        float factor = keyed ? 1.0f : DifficultyAttackFactor();
        float roll = factor * stats.combat[UnitStats::CombatIndex::Attack];
        out->attackRoll = roll;
        float extra = 0.0f;
        float spread = stats.damageSpread;
        if (spread != 0.0f) {
            float scaled = spread * kSpreadSteps;
            int sides = lib::RoundEven(scaled);
            std::uint32_t random = g_random.Next();
            extra = static_cast<float>(random % static_cast<std::uint32_t>(sides + 1)) * kPercent;
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

void UnitServer::TakeHit(DamageInfo* hit, Unit* attacker, Unit* source) {
    WorldServer* world = ServerWorld();
    int dead = unit->IsDead();
    bool hadPlayer = unit->player != nullptr;
    const float rolled = hit->attackRoll;   // logged as rolled, before ResolveHit marks it resolved
    // |attackRoll| below FLT_MAX: not resolved yet.
    if (AbsBits(rolled) < kHitMark) ResolveHit(hit, attacker);
    if (!dead) {
        world->AddToUpdateMap(unit);
        if (attacker) {
            kCombatLog << "Unit " << static_cast<long>(unit->id) << " was attacked by " << static_cast<long>(attacker->id);
        } else {
            kCombatLog << "Unit " << static_cast<long>(unit->id) << " was attacked by xz";
        }
        kCombatLog << " skill=" << rolled;
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
        if (PlayerGodMode(player) != 0) return;
        if (source && source->player && player != source->player) return;
    }

    AttackParams mine;
    AttackParamsOf(this, &mine);
    if (hit->backstabFactor > 1.0f) mine.defence = 0;
    if (static_cast<float>(mine.defence) <= hit->attackRoll) {
        stats.flags |= UnitStatsFlag::Struck;
        unit->MarkChanged(NetField::Stats);
        TriggerItemSpells(1);
        Item* weapon = attacker ? attacker->GetWeapon(attacker->activeWeapon) : nullptr;
        if (stats.ApplyHit(unit, &unit->Vitals(), hit, weapon)) {
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
            stats.Recompute(&unit->Vitals(), unit);
            unit->MarkChanged(NetField::Vitals);
            unit->MarkChanged(NetField::Stats);
            if (!dead) {
                OnDamaged(attacker, hit->hitKind);
                if (unit->IsDead() != 0 && source && source->player && !hadPlayer) {
                    const auto* prototype = unit->GetPrototype();
                    if (IsMultiplayerClient()) {
                        world->AwardExperience(nullptr, prototype->experienceValue);
                        return;
                    }
                    Player* killer = source->player;
                    world->AwardExperience(killer, prototype->experienceValue);
                }
            }
            return;
        }
    }
    if (attacker && attacker->GetClassId() == ClassId::Unit) {
        auto* diplomacy = &ServerWorld()->diplomacy;
        diplomacy->OnAttack(attacker, unit);
    }
    brain.SetSeen(attacker);
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
    float scaled = (factor * stats.perception[Sense::DaySight].current) * kWorldFactorScale;
    moveFactors[MoveFactor::Noise] = 1.0f;
    moveFactors[MoveFactor::Visibility] = 1.0f;
    moveFactors[MoveFactor::Hearing] = 1.0f;
    float sum = scaled + (1.0f - factor);
    moveFactors[MoveFactor::Sight] = world->weatherFactor * sum;
    if (unit->moveMode == MoveMode::Lie) {
        moveFactors[MoveFactor::Visibility] = kMode0Factor2;
    } else if (unit->moveMode == MoveMode::Crawl) {
        moveFactors[MoveFactor::Visibility] = kMode1Factor2;
    }
    if (state == UnitState::Idle) {
        if (unit->moveMode == MoveMode::Lie) moveFactors[MoveFactor::Noise] = kIdleMode0Factor0;
        else if (unit->moveMode == MoveMode::Crawl) moveFactors[MoveFactor::Noise] = kIdleMode1Factor0;
        if (unit->moveMode == MoveMode::Run) {
            moveFactors[MoveFactor::Noise] = kIdleMode3Scale * moveFactors[MoveFactor::Noise];
            moveFactors[MoveFactor::Hearing] = kIdleMode3Factor1;
        }
    } else {
        moveFactors[MoveFactor::Noise] = 0.0f;
    }
    if (state == UnitState::Acting && (action.kind == UnitOrderKind::CastAt || action.kind == UnitOrderKind::CastOn)) {
        Spell* spell = SpellOfSource(action.source);
        if (spell && spell->GetPrototype()->castType != 1)
            moveFactors[MoveFactor::Visibility] = kCastingFactor2 * moveFactors[MoveFactor::Visibility];
    }
    if (state != UnitState::Idle) {
        speedClass = 15;
        return;
    }
    float speed = unit->MoveSpeed();
    if (speed > kSpeedFast) speedClass = 0;
    else if (speed > kSpeedMedium) speedClass = 1;
    else if (speed > kPointThree) speedClass = 3;
    else if (speed > kSpeedSlow) speedClass = 7;
    else speedClass = 15;
}

namespace {
const char* const kServerName = "CUnitServer";
} // namespace

// --- UnitServer's lifetime, class name and id -----------------------------------------------------

// The fields start at their initialisers; Create resets the brain, the statistics and the orders (ResetController).
// The manager destroys an object before it deletes it, so the destructor finds nothing left to release.
UnitServer::UnitServer() = default;
UnitServer::~UnitServer() = default;

const char* UnitServer::ClassName() { return kServerName; }
ClassId UnitServer::GetClassId() { return ClassId::UnitServer; }

void UnitServer::MoveFactorsChanged() { UpdateMoveFactors(); }

// --- OrderPlan ----------------------------------------------------------------------------------

void OrderPlan::Reset() { *this = {.direction = direction}; }

void OrderPlan::TakeSteps(AnimationSteps steps, int start, int duration) {
    Reset();
    startTime = start;
    endTime = start + duration;
    animation = std::move(steps);
}

// --- Other objects and the unit's own setup: hostility claims, taking up a weapon, an object being deleted,
// settling the unit again.

namespace {
constexpr int kCallClaim = 0x1E;        // the kind of the AI memory's call for a claim


// The weapon's condition rounded to nearest is above 0.
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
    unit->MarkChanged(NetField::Modes);
    unit->activeWeapon = static_cast<std::uint32_t>(taken);
    unit->pendingWeapon = taken;
    if (unit->player) brain.attention.Build(unit);
    RefreshItemSpells();
    return Usable(weapon);
}

void UnitServer::OnObjectDeleted(EngineObject* object) {
    if (!object) return;
    if (action.target == object) {
        EndCurrentOrder();
    }
    if (action2.target == object) Idle();
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
            map->sectorMap.Add(unit);
            return;
        }
    }
    map->sectorMap.Add(unit);
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



} // namespace

void UnitServer::RefreshVitals(bool withHitPoints) {
    int now = OwningWorld()->tick;
    float oldRate = stats.rate;
    if (withHitPoints) stats.RecomputeHitPoints(&unit->Vitals(), unit);
    stats.Recompute(&unit->Vitals(), unit);
    if (nextAttackTime > now) {
        float scaled = (static_cast<float>(nextAttackTime - now) * oldRate) / stats.rate;
        nextAttackTime = lib::RoundEven(scaled) + now;
    }
    unit->MarkChanged(NetField::Vitals);
}

void UnitServer::UpdateProtection() {
    stats.PackBodyProtection(unit, &unit->Vitals(), &protection);
    float factor = unit->player ? 1.0f : DefenceFactors()[options::g_options.difficulty];
    ItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
    if (unit->player && weapon) {
        float bonus = stats.WeaponSkillBonus(weapon->GetType(), unit);
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(bonus * factor));
    } else {
        protection.words[0] = static_cast<std::uint16_t>(static_cast<int>(factor * stats.combat[UnitStats::CombatIndex::Defence]));
    }
}

void UnitServer::RepackStats() {
    std::uint8_t speedsBefore[sizeof(unit->params.modeSpeeds)];
    std::memcpy(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore));
    stats.Pack(&unit->params);
    stats.flags = 0;
    UpdateProtection();
    unit->MarkChanged(NetField::Stats);
    if (std::memcmp(speedsBefore, unit->params.modeSpeeds, sizeof(speedsBefore)) != 0) motionChanged = 1;
}

void UnitServer::ArmourPutOn(std::uint32_t key) {
    // A key out of range gives no record and adds nothing.
    if (auto* record = itemdb::ArmorAt(key >> 16)) stats.AddArmourLayer(unit->armor[record->type]);
}

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
