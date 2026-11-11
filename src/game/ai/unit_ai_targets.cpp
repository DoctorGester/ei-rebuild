// The unit AI's target choice: scoring an attack option against a unit, whether an option may be used,
// choosing the best target, the orders it checks and whether the unit can get at a target; and the attack
// options themselves (AIAttention, unit_ai.h). See docs/notes/unit_ai.md.
#include "game/ai/ai_map.h"
#include "game/combat/combat.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/world/diplomacy.h"
#include "engine/data/text_manager.h"
#include "game/saves/save_versions.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/rounding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace {
constexpr float kCurrentBonus = 10.0f;
constexpr float kGroupBase = 3.0f;
constexpr float kDamageWeight = 1.0f;
constexpr float kGroupDamageWeight = 1.3f;
constexpr float kHealFactor = 3.0f;
constexpr float kDistanceWeight = 25.0f;
constexpr float kHitWeight = 2.0f;
constexpr float kHealGroupBase = 2.0f;
constexpr float kAllyNear2 = 49.0f;             // allies within 7 of an area target
constexpr float kPathNear = 30.0f;
constexpr int kNoHitsKnown = 20;
constexpr float kNoScore = -1e30f;
constexpr float kFloorScore = -1e20f;     // the starting best
constexpr float kSmallHeight = 0.5f;            // prototypes below this height count as small units

// An area attack's damage: summed over the group members within the option's radius of the candidate (NaN
// distances count).
std::uint32_t GroupExcess(const AIAttackOption& option, const UnitList& group, Unit* candidate) {
    std::uint32_t sum = 0;
    if (group.empty()) return sum;
    float radius2 = option.radius * option.radius;
    for (Unit* member : group) {
        float distance2 = DistanceSquaredXY(member->localPosition, candidate->localPosition);
        if (distance2 <= radius2) sum += option.damage.ExcessOver(&member->ServerController()->protection);
    }
    return sum;
}

// A scored {part, object} the choice keeps.
struct Scored {
    int part = 0;
    Unit* object = nullptr;
    float score = 0.0f;
};

// Whether the controller's order check lets the unit act on `choice` from `from`: an attack for the weapon part,
// else a cast.
bool OrderAllowedFrom(AIBrain* brain, const AITargetChoice& choice, const Vector3& from) {
    AIOrder order;
    if (choice.part == kWeaponPart)
        order.MakeAttack(choice.object);
    else
        order.MakeCast(choice.part, choice.object);
    return brain->owner->CanReachAndPlanFrom(&order, from);
}
} // namespace

// --- Whether the unit can get at a target ------------------------------------------------------------

bool AIBrain::CanReach(const AITargetChoice& choice, bool fromHere) {
    AIPath path;
    Unit* self = owner->unit;
    Unit* object = choice.object;
    int part = choice.part;
    // A spell aimed at no object cannot be cast on a large unit of size class 0.
    if (part >= 0) {
        Spell* spell = self->GetSpellObject(part);
        if (spell && (spell->targetFlags & SpellTargetFlag::AtPoint) && object->sizeClass == 0 &&
            object->GetPrototype()->height >= kSmallHeight)
            return false;
    }

    // Where the order is checked from: the unit's place, or the end of a path towards the target.
    Vector3 point;
    if (self == object || fromHere) {
        point = self->localPosition;
    } else {
        float distance = std::sqrt(LengthSquared(self->localPosition - object->localPosition));
        float range = self->ScaledValue(distance);
        float targetRadius = object->Radius();
        float minDistance = self->Radius() + targetRadius;
        // The target is lifted off the sector map so it does not block its own cell.
        g_serverWorld.aiMap.sectorMap.Remove(object);
        if (g_serverWorld.aiMap.FindPath(self, object->localPosition, &path, range, minDistance)) {
            point = path.TargetPoint();
        } else {
            point = self->localPosition;
        }
        g_serverWorld.aiMap.sectorMap.Add(object);
    }
    return OrderAllowedFrom(this, choice, point);
}

const AIAttackOption* AIAttention::FindOption(int part) const {
    for (const AIAttackOption& option : weapons)
        if (option.part == part) return &option;
    for (const AIAttackOption& option : spells)
        if (option.part == part) return &option;
    return nullptr;
}

bool AIBrain::IsUsable(const AITargetChoice& choice) {
    Unit* object = choice.object;
    if (!object) return true;
    Unit* self = owner->unit;
    const AIAttackOption* option = attention.FindOption(choice.part);
    if (!option) return false;
    if (option->kind != AIOptionKind::AreaAttack && option->kind != AIOptionKind::LastingArea) return true;
    if (self->sizeClass == 0 && self->GetPrototype()->height >= kSmallHeight) return true;
    Vector3 at = object->localPosition;
    for (Unit* other : memory.nearby) {
        if (DistanceSquaredXY(at, other->localPosition) < kAllyNear2 &&
            g_serverWorld.diplomacy.IsAllied(other->side, self->side)) {
            if (other->sizeClass != 0) return false;
            if (other->GetPrototype()->height < kSmallHeight) return false;
        }
    }
    float radius = option->radius;
    return radius * radius < DistanceSquaredXY(self->localPosition, at);
}

namespace {
// How good `option` is against `candidate` (`group` the others it would hit or help): by kind, minus 25 per unit of
// distance beyond the range (x10 outside kinds 0..4) when it reached `threshold`, minus twice the hits needed for
// attacks. -1e30 when it cannot apply.
float Score(const Vector3& from, Unit* self, const UnitList& group, EngineObject* current, Unit* candidate,
            float threshold, const AIAttackOption& option) {
    float score = candidate == current ? kCurrentBonus : 0.0f;
    UnitServer* control = candidate->ServerController();
    int others = lib::Count(group) - 1;
    int cap = option.maxCount;
    std::uint32_t damage = 0;
    int words = 0;              // the damage's low 16 bits: all that counts
    switch (option.kind) {
    case AIOptionKind::Weapon:
        damage = option.damage.ExcessOver(&candidate->ServerController()->protection);
        words = static_cast<int>(damage & 0xFFFF);
        score = static_cast<float>(words) * kDamageWeight + score;
        break;
    case AIOptionKind::SpellAttack:
    case AIOptionKind::AreaAttack:
    case AIOptionKind::LastingArea: {
        if (option.kind == AIOptionKind::SpellAttack) {
            damage = option.damage.ExcessOver(&candidate->ServerController()->protection);
        } else {
            if (option.kind == AIOptionKind::LastingArea && control->state == UnitState::Idle) return kNoScore;
            damage = GroupExcess(option, group, candidate);
            // A lasting area hits once per eighth of its duration (divided as the original does).
            if (option.kind == AIOptionKind::LastingArea) damage *= static_cast<std::uint32_t>(option.effect / 4 / 2);
            ++others;   // the group without the one left out
        }
        words = static_cast<int>(damage & 0xFFFF);
        float partial = static_cast<float>(words) * kGroupDamageWeight + score + kGroupBase;
        int count = (std::min)(others, cap);
        score = static_cast<float>(words * count) + partial;
        break;
    }
    case AIOptionKind::Curse:
    case AIOptionKind::Protect:
    case AIOptionKind::Enhance: {
        if (candidate->HasEffect(option.EffectSpell())) return kNoScore;
        float partial = score + 1.0f;
        int count = (std::min)(others, cap);
        score = static_cast<float>(count) + partial;
        break;
    }
    case AIOptionKind::Heal: {
        float base = kGroupBase + score;
        int hp = candidate->packedVitals.hitPoints;
        int limit = lib::RoundEven(control->stats.ReduceKind1(static_cast<float>(candidate->packedVitals.maxHitPoints)));
        if (hp > limit - 2) return kNoScore;
        if (hp * 3 < limit) {
            float weight = control->brain.attention.healWeight;
            score = kHealFactor * weight;
            if (self->packedVitals.hitPoints == self->packedVitals.maxHitPoints) score = score + weight;
        } else {
            float partial = base + kHealGroupBase;
            int count = (std::min)(others, cap);
            score = static_cast<float>(count * 2) + partial;
        }
        break;
    }
    default:
        break;
    }
    if (score < threshold) return score;
    float reach = 0.0f;
    float scale = 1.0f;
    if (option.kind >= AIOptionKind::Protect) {   // the helping kinds count their range
        reach = option.range;
        scale = 10.0f;
    }
    float beyond = std::sqrt(DistanceSquaredXY(candidate->localPosition, from)) - reach;
    float penalty = MaxKeepB(0.0f, beyond);
    float result = score - penalty * kDistanceWeight * scale;
    if (result < threshold) return result;
    if (option.kind >= AIOptionKind::Weapon && option.kind <= AIOptionKind::LastingArea) {
        int hits = words == 0 ? kNoHitsKnown : candidate->packedVitals.hitPoints / words;
        result -= static_cast<float>(hits) * kHitWeight;
    }
    return result;
}
} // namespace

AITargetChoice AIBrain::Choose(const UnitList& candidates, const Vector3& from, bool alliesOnly, bool fromHere) {
    const std::vector<AIAttackOption>& weapons = attention.weapons;
    const std::vector<AIAttackOption>& spells = attention.spells;
    if (spells.empty() && (candidates.empty() || weapons.empty())) return {};
    AITargetChoice attack = CurrentAttack();
    if (!IsUsable(attack)) attack.object = nullptr;
    Unit* held = attack.object;
    float best = kFloorScore;
    Unit* self = owner->unit;
    if (held) {
        if (candidates.size() == 1 && spells.empty()) return attack;
        const AIAttackOption* option = attention.FindOption(attack.part);
        if (!option) {
            held = nullptr;
            attack.object = nullptr;
        } else {
            best = Score(from, self, candidates, held, held, kFloorScore, *option);
            if (best < kFloorScore) {
                attack.object = nullptr;
                best = kFloorScore;
            }
        }
    }

    std::vector<Scored> scored;
    for (Unit* candidate : candidates) {
        for (const AIAttackOption& weapon : weapons) {
            float s = Score(from, self, candidates, held, candidate, best, weapon);
            if (s > best) scored.push_back({weapon.part, candidate, s});
        }
    }
    if (!spells.empty()) {
        // The unit and its allies in sight that are alive and not reported.
        UnitList allies;
        allies.push_back(self);
        for (Unit* other : memory.nearby) {
            if (!other->IsDead() && !memory.reports.Contains(other) && g_serverWorld.diplomacy.IsAllied(self->side, other->side))
                allies.push_back(other);
        }
        for (Unit* ally : allies) {
            for (const AIAttackOption& option : spells) {
                if (alliesOnly && option.kind == AIOptionKind::Enhance) continue;
                float s = Score(from, self, allies, held, ally, best, option);
                if (s > best) scored.push_back({option.part, ally, s});
            }
        }
    }

    while (!scored.empty()) {
        AITargetChoice choice = attack;
        int chosen = -1;
        float top = kNoScore;
        for (int i = 0; i < static_cast<int>(scored.size()); ++i) {
            if (top < scored[i].score) {
                choice = {scored[i].part, scored[i].object};
                chosen = i;
                top = scored[i].score;
            }
        }
        if (chosen < 0) return choice;
        if (choice.object == attack.object && choice.part == attack.part) return choice;
        Unit* object = choice.object;
        if (IsUsable(choice)) {
            // Allies only: a target off the path must be in reach from here.
            bool allowed = !alliesOnly || IsNearPath(object->localPosition, kPathNear) ||
                           OrderAllowedFrom(this, choice, self->localPosition);
            if (allowed && CanReach(choice, fromHere)) return choice;
        }
        // Rejected: an object scored only once (and not the current target) is reported.
        int times = 0;
        for (const Scored& entry : scored)
            if (entry.object == object) ++times;
        if (times == 1 && object != attack.object) memory.reports.Report(object, AIReports::kRejectedTicks);
        scored.erase(scored.begin() + chosen);
    }
    return attack;
}

// --- Attack options: listing them from the unit's weapon and spells, and the option helpers --------------

namespace {
constexpr float kAttackSpellWeight = 1.3f;
constexpr float kHelpWeight = 1.0f;
constexpr float kHealWeight = 10.0f;

// The option kind of a spell by its use (Spell::Use): 1..7, 8 for none; 0 without a spell.
AIOptionKind SpellKind(const Spell* spell) {
    if (!spell) return AIOptionKind::Weapon;
    switch (spell->Use().ai) {
    case SpellAi::Attack: return AIOptionKind::SpellAttack;
    case SpellAi::AreaAttack: return AIOptionKind::AreaAttack;
    case SpellAi::LastingArea: return AIOptionKind::LastingArea;
    case SpellAi::Curse: return AIOptionKind::Curse;
    case SpellAi::Protect: return AIOptionKind::Protect;
    case SpellAi::Enhance: return AIOptionKind::Enhance;
    case SpellAi::Heal: return AIOptionKind::Heal;
    case SpellAi::None: break;
    }
    return AIOptionKind::Unused;
}
} // namespace

float AIAttackOption::Weight() const {
    switch (kind) {
    case AIOptionKind::Weapon:
        return MaxDamage(damage.words);
    case AIOptionKind::SpellAttack:
    case AIOptionKind::AreaAttack:
    case AIOptionKind::LastingArea:
        return static_cast<float>(MaxDamage(damage.words)) * kAttackSpellWeight;
    case AIOptionKind::Curse:
    case AIOptionKind::Protect:
    case AIOptionKind::Enhance:
        return kHelpWeight;
    case AIOptionKind::Heal:
        return kHealWeight;
    default:
        return 0.0f;
    }
}

void AIAttackOption::Load(ByteStream* stream) {
    if (stream->version < kVersionSpellOptions) {
        *this = AIAttackOption{.range = 1.0f};
        stream->ReadValue(part);
        stream->Skip(4);
        stream->ReadValue(damage);
        kind = part >= 0 ? AIOptionKind::SpellAttack : AIOptionKind::Weapon;
        return;
    }
    stream->ReadValue(kind);
    stream->ReadValue(part);
    stream->ReadValue(range);
    stream->ReadValue(maxCount);
    stream->ReadValue(radius);
    stream->ReadValue(effect);
    stream->ReadValue(damage);
}

void AIAttackOption::Save(ByteStream* stream) const {
    stream->WriteValue(kind);
    stream->WriteValue(part);
    stream->WriteValue(range);
    stream->WriteValue(maxCount);
    stream->WriteValue(radius);
    stream->WriteValue(effect);
    stream->WriteValue(damage);
}

void AIAttention::Build(Unit* unit) {
    auto* server = unit->ServerController();
    weapons.clear();
    spells.clear();
    const bool textKey = IsTextKey(unit->id);
    if (textKey || server->stats.naturalWeapon != WeaponType::SpellsOnly) {
        AIAttackOption option{.kind = AIOptionKind::Weapon, .part = kWeaponPart, .range = server->AttackRange()};
        server->GetDamageByType(&option.damage);
        weapons.push_back(option);
    }
    if (!textKey) {
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const Spell* spell = unit->GetSpellObject(slot);
            if (!spell) continue;
            AIOptionKind kind = SpellKind(spell);
            if (kind == AIOptionKind::Unused) continue;
            float range = spell->range;
            if (server->stats.naturalWeapon == WeaponType::SpellsOnly) range = server->stats.combat[UnitStats::CombatIndex::Reach];
            std::uint32_t count = spell->targetCount;
            int maxCount = static_cast<int>((count < 1 ? 1 : count) - 1);
            float radius = 0.0f;
            int effect = 0;
            switch (kind) {
            case AIOptionKind::AreaAttack:
                radius = spell->radius;
                break;
            case AIOptionKind::LastingArea:
                radius = spell->radius;
                effect = static_cast<int>(spell->duration);
                break;
            case AIOptionKind::Curse:
            case AIOptionKind::Protect:
            case AIOptionKind::Enhance:
                effect = static_cast<int>(spell->Use().effect);
                break;
            case AIOptionKind::Heal:
                radius = spell->damage;
                break;
            default:
                break;
            }
            AIAttackOption option{.kind = kind,
                                  .part = static_cast<int>(slot),
                                  .range = range,
                                  .maxCount = maxCount,
                                  .radius = radius,
                                  .effect = effect};
            switch (kind) {
            case AIOptionKind::SpellAttack:
            case AIOptionKind::AreaAttack:
            case AIOptionKind::LastingArea:
                server->GetSpellDamage(slot, &option.damage);
                weapons.push_back(option);
                break;
            case AIOptionKind::Curse:
                weapons.push_back(option);
                break;
            case AIOptionKind::Protect:
            case AIOptionKind::Enhance:
            case AIOptionKind::Heal:
                spells.push_back(option);
                break;
            default:
                break;
            }
        }
    }
    healWeight = 0.0f;
    for (const AIAttackOption& option : weapons) healWeight = MaxKeepB(healWeight, option.Weight());
}

// The count, then each option.
std::vector<AIAttackOption> LoadAttackOptions(ByteStream* stream) {
    std::vector<AIAttackOption> options(stream->ReadCount());
    for (AIAttackOption& option : options) option.Load(stream);
    return options;
}
