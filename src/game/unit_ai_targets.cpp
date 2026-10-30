// The unit AI's target choice: scoring an attack option against a unit, whether an option may be used,
// choosing the best target, the orders it checks and whether the unit can get at a target; and the attack
// options themselves (AIAttention, unit_ai.h). See docs/notes/unit_ai.md.
#include "ai_map.h"
#include "combat.h"
#include "game_singletons.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "object_tables.h"
#include "save_versions.h"
#include "sector_map.h"
#include "spell.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
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
static_assert(std::bit_cast<std::uint32_t>(kNoScore) == 0xF149F2CAu);
constexpr float kFloorScore = -1e20f; // and the starting best: -1e20
static_assert(std::bit_cast<std::uint32_t>(kFloorScore) == 0xE0AD78ECu);
constexpr float kSmallHeight = 0.5f;            // prototypes below this height count as small units


// An area attack's damage: summed over the group members within the option's radius of the candidate (NaN
// distances count).
std::uint32_t GroupExcess(const AIAttackOption* option, UnitList* group, CUnit* candidate) {
    std::uint32_t sum = 0;
    if (group->empty()) return sum;
    float radius2 = option->radius * option->radius;
    for (auto node = group->begin(); node != group->end(); ++node) {
        auto* member = *node;
        float distance2 = DistanceSquaredXYUnrounded<float>(member->localPosition, candidate->localPosition);
        EI_NAN_AUDIT(distance2, radius2);
        if (distance2 <= radius2) sum += option->damage.ExcessOver(&member->ServerController()->protection);
    }
    return sum;
}

// A scored {part, object} the choice keeps.
struct Scored {
    int part;
    CUnit* object;
    float score;
};

// Whether the controller's order check lets the unit act on `choice` from `from`: an attack for the weapon part,
// else a cast.
bool OrderAllowedFrom(AIBrain* brain, const AITargetChoice* choice, const Vector3& from) {
    AIOrder order;
    if (choice->part == kWeaponPart)
        order.MakeAttack(choice->object);
    else
        order.MakeCast(choice->part, choice->object);
    return brain->owner->CanReachAndPlanFrom(&order, from);
}

// An attack option with its damage zeroed.
AIAttackOption MakeOption(AIOptionKind kind, int part, float range, int maxCount, float radius, int effect) {
    AIAttackOption option{};
    option.part = part;
    option.range = range;
    option.kind = kind;
    option.maxCount = maxCount;
    option.radius = radius;
    option.effect = effect;
    return option;
}
} // namespace

void AIOrder::Clear() {
    kind = UnitOrderKind::None;
    target.Reset();
    pointIsUseSpot = false;
}

void AIOrder::MakeAttack(CMapObject* object) {
    Clear();
    kind = UnitOrderKind::Attack;
    source = kAttackAllParts;
    target.Assign(object);
}

void AIOrder::MakeCast(int spell, CMapObject* object) {
    Clear();
    kind = UnitOrderKind::CastOn;
    target.Assign(object);
    source = static_cast<std::uint32_t>(spell);
}

void AIOrder::MakeUse(CMapObject* object, UseSource use) {
    Clear();
    kind = UnitOrderKind::Use;
    target.Assign(object);
    source = static_cast<std::uint32_t>(use);
}

Vector3* AIOrder::AimPoint(Vector3* out) const {
    if (target == nullptr) {
        *out = point;
        return out;
    }
    target->AimPoint(out);
    return out;
}

bool AIBrain::CanReach(const AITargetChoice* choice, bool fromHere) {
    AIPath path;
    CUnit* self = owner->unit;
    CUnit* object = choice->object;
    int part = choice->part;
    // A spell aimed at no object cannot be cast on a large unit of size class 0.
    if (part >= 0) {
        CSpell* spell = self->GetSpellObject(part);
        if (spell && (spell->targetFlags & SpellTargetFlag::AtPoint) && object->sizeClass == 0 &&
            object->GetPrototype()->height >= kSmallHeight)
            return false;
    }

    // Where the order is checked from: the unit's place, or the end of a path towards the target.
    Vector3 point;
    if (self == object || fromHere) {
        point = self->localPosition;
    } else {
        float dz = self->localPosition.z - object->localPosition.z;
        float dy = self->localPosition.y - object->localPosition.y;
        float dx = self->localPosition.x - object->localPosition.x;
        float distance = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz));
        float range = self->ScaledValue(distance);
        float targetRadius = object->Radius();
        float minDistance = self->Radius() + targetRadius;
        WorldServer* server = &g_serverWorld;
        // The target is lifted off the sector map so it does not block its own cell.
        server->aiMap.sectorMap.Remove(object);
        if (server->aiMap.FindPath(self, object->localPosition, &path, range, minDistance)) {
            float scale = 0.5f;
            point.x = scale * path.targetX;
            point.y = scale * path.targetY;
            point.z = 0.0f;
        } else {
            point = self->localPosition;
        }
        server->aiMap.sectorMap.Add(object);
    }
    return OrderAllowedFrom(this, choice, point);
}

const AIAttackOption* AIAttention::FindOption(int part) const {
    for (int i = 0; i < std::ssize(weapons); ++i)
        if (weapons[i].part == part) return &weapons[i];
    for (int i = 0; i < std::ssize(spells); ++i)
        if (spells[i].part == part) return &spells[i];
    return nullptr;
}

bool AIBrain::IsUsable(const AITargetChoice* choice) {
    CUnit* object = choice->object;
    if (!object) return true;
    CUnit* self = owner->unit;
    const AIAttackOption* option = attention.FindOption(choice->part);
    if (!option) return false;
    if (option->kind != AIOptionKind::AreaAttack && option->kind != AIOptionKind::LastingArea) return true;
    EI_NAN_AUDIT(self->GetPrototype()->height);
    if (self->sizeClass == 0 && self->GetPrototype()->height >= kSmallHeight) return true;
    Vector3 at = object->localPosition;
    for (auto& node : memory.nearby) {
        CUnit* other = node;
        float dy = at.y - other->localPosition.y;
        float dx = at.x - other->localPosition.x;
        EI_NAN_AUDIT(dx, dy, other->GetPrototype()->height);
        if (LengthSquared2DUnrounded(dx, dy) < kAllyNear2 && g_serverWorld.diplomacy.IsAllied(other->side, self->side)) {
            if (other->sizeClass != 0) return false;
            if (other->GetPrototype()->height < kSmallHeight) return false;
        }
    }
    float dy = self->localPosition.y - at.y;
    float dx = self->localPosition.x - at.x;
    float radius = option->radius;
    EI_NAN_AUDIT(radius, dx, dy);
    return radius * radius < LengthSquared2DUnrounded(dx, dy);
}

namespace {
// How good `option` is against `candidate` (`group` the others it would hit or help): by kind, minus 25 per unit of
// distance beyond the range (x10 outside kinds 0..4) when it reached `threshold`, minus twice the hits needed for
// attacks. -1e30 when it cannot apply.
float Score(const Vector3& from, CUnit* self, UnitList* group, EngineObject* current, CUnit* candidate, float threshold,
             const AIAttackOption* option) {
    float score = candidate == current ? kCurrentBonus : 0.0f;
    UnitServer* control = candidate->ServerController();
    int others = lib::Count(*group) - 1;
    int cap = option->maxCount;
    std::uint32_t damage = 0;
    int words = 0;              // the damage's low 16 bits: all that counts
    switch (option->kind) {
    case AIOptionKind::Weapon:
        damage = option->damage.ExcessOver(&candidate->ServerController()->protection);
        words = static_cast<int>(damage & 0xFFFF);
        score = static_cast<float>(words) * kDamageWeight + score;
        break;
    case AIOptionKind::SpellAttack:
    case AIOptionKind::AreaAttack:
    case AIOptionKind::LastingArea: {
        if (option->kind == AIOptionKind::SpellAttack) {
            damage = option->damage.ExcessOver(&candidate->ServerController()->protection);
        } else {
            if (option->kind == AIOptionKind::LastingArea && control->state == UnitState::Idle) return kNoScore;
            damage = GroupExcess(option, group, candidate);
            // A lasting area hits once per eighth of its duration (divided as the original does).
            if (option->kind == AIOptionKind::LastingArea) damage *= static_cast<std::uint32_t>(option->effect / 4 / 2);
            ++others;   // the group without the one left out
        }
        words = static_cast<int>(damage & 0xFFFF);
        float partial = static_cast<float>(words) * kGroupDamageWeight + score + kGroupBase;
        int count = (std::min)(others, cap);
        score = pc24::Add(static_cast<double>(words * count), partial);   // the int exactly (data may pass 2^24)
        break;
    }
    case AIOptionKind::Curse:
    case AIOptionKind::Protect:
    case AIOptionKind::Enhance: {
        if (candidate->HasEffect(option->EffectSpell())) return kNoScore;
        float partial = score + 1.0f;
        int count = (std::min)(others, cap);
        score = pc24::Add(static_cast<double>(count), partial);
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
            score = pc24::Add(static_cast<double>(count * 2), partial);
        }
        break;
    }
    default:
        break;
    }
    EI_NAN_AUDIT(score, threshold);
    if (score < threshold) return score;
    float reach = 0.0f;
    float scale = 1.0f;
    if (option->kind >= AIOptionKind::Protect) {   // the helping kinds count their range
        reach = option->range;
        scale = 10.0f;
    }
    float beyond = std::sqrt(DistanceSquaredXYUnrounded<float>(candidate->localPosition, from)) - reach;
    float penalty = MaxKeepB(0.0f, beyond);
    float result = score - penalty * kDistanceWeight * scale;
    EI_NAN_AUDIT(result);
    if (result < threshold) return result;
    if (option->kind >= AIOptionKind::Weapon && option->kind <= AIOptionKind::LastingArea) {
        int hits = words == 0 ? kNoHitsKnown : candidate->packedVitals.hitPoints / words;
        result -= static_cast<float>(hits) * kHitWeight;
    }
    return result;
}
} // namespace

AITargetChoice AIBrain::Choose(UnitList* candidates, const Vector3& from, bool alliesOnly, bool fromHere) {
    const std::vector<AIAttackOption>& weapons = attention.weapons;
    const std::vector<AIAttackOption>& spells = attention.spells;
    if (spells.empty() && (candidates->empty() || weapons.empty())) return {};
    AITargetChoice attack = CurrentAttack();
    if (!IsUsable(&attack)) attack.object = nullptr;
    CUnit* held = attack.object;
    float best = kFloorScore;
    CUnit* self = owner->unit;
    if (held) {
        if (candidates->size() == 1 && spells.empty()) return attack;
        const AIAttackOption* option = attention.FindOption(attack.part);
        if (!option) {
            held = nullptr;
            attack.object = nullptr;
        } else {
            best = Score(from, self, candidates, held, held, kFloorScore, option);
            EI_NAN_AUDIT(best);
            if (best < kFloorScore) {
                attack.object = nullptr;
                best = kFloorScore;
            }
        }
    }

    std::vector<Scored> scored;
    if (!alliesOnly) {
        for (auto node = candidates->begin(); node != candidates->end(); ++node) {
            auto* candidate = *node;
            for (int i = 0; i < std::ssize(weapons); ++i) {
                float s = Score(from, self, candidates, held, candidate, best, &weapons[i]);
                if (s > best) scored.push_back({weapons[i].part, candidate, s});
            }
        }
    }
    if (!spells.empty()) {
        // The unit and its allies in sight that are alive and not reported.
        UnitList allies;
        allies.push_back(self);
        for (auto& node : memory.nearby) {
            CUnit* other = node;
            if (other->IsDead() == 0 && !memory.reports.Contains(other) && g_serverWorld.diplomacy.IsAllied(self->side, other->side))
                allies.push_back(other);
        }
        for (auto& node : allies) {
            auto* ally = node;
            for (int i = 0; i < std::ssize(spells); ++i) {
                const AIAttackOption* option = &spells[i];
                if (alliesOnly && option->kind == AIOptionKind::Enhance) continue;
                float s = Score(from, self, &allies, held, ally, best, option);
                if (s > best) scored.push_back({option->part, ally, s});
            }
        }
    }

    while (!scored.empty()) {
        AITargetChoice choice = attack;
        int chosen = -1;
        float top = kNoScore;
        for (int i = 0; i < static_cast<int>(scored.size()); ++i) {
            EI_NAN_AUDIT(top, scored[i].score);
            if (top < scored[i].score) {
                choice = {scored[i].part, scored[i].object};
                chosen = i;
                top = scored[i].score;
            }
        }
        if (chosen < 0) return choice;
        if (choice.object == attack.object && choice.part == attack.part) return choice;
        CUnit* object = choice.object;
        if (IsUsable(&choice)) {
            // Allies only: a target off the path must be in reach from here.
            bool allowed = !alliesOnly || IsNearPath(object->localPosition, kPathNear) ||
                           OrderAllowedFrom(this, &choice, self->localPosition);
            if (allowed && CanReach(&choice, fromHere)) return choice;
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

std::uint16_t MaxDamage(const std::uint16_t*);
namespace {
constexpr int kNaturalSpellsOnly = 0x10;    // natural weapon type of units that only cast
constexpr float kAttackSpellWeight = 1.3f;
constexpr float kHelpWeight = 1.0f;
constexpr float kHealWeight = 10.0f;

// The option kind per spell type.
constexpr AIOptionKind kSpellKinds[42] = {
    AIOptionKind::SpellAttack, AIOptionKind::SpellAttack, AIOptionKind::SpellAttack, AIOptionKind::AreaAttack, AIOptionKind::AreaAttack, AIOptionKind::AreaAttack,
    AIOptionKind::LastingArea, AIOptionKind::LastingArea, AIOptionKind::LastingArea, AIOptionKind::Protect, AIOptionKind::Protect, AIOptionKind::Protect,
    AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance, AIOptionKind::Enhance,
    AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused,
    AIOptionKind::Heal, AIOptionKind::Curse, AIOptionKind::Protect, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Protect,
    AIOptionKind::Curse, AIOptionKind::Enhance, AIOptionKind::Curse, AIOptionKind::Enhance, AIOptionKind::Unused, AIOptionKind::Unused,
    AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::Unused, AIOptionKind::SpellAttack, AIOptionKind::SpellAttack};

float OptionWeight(const AIAttackOption*);

float OptionWeight(const AIAttackOption* option) { return option->Weight(); }

// The option kind of a spell by its type: 1..7, 8 for none; 0 without a spell.
AIOptionKind SpellKind(const CSpell* spell) {
    if (!spell) return AIOptionKind::Weapon;
    unsigned type = static_cast<unsigned>(spell->Type());
    return type < std::size(kSpellKinds) ? kSpellKinds[type] : AIOptionKind::Unused;
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
        stream->ReadValue(part);
        std::uint32_t dropped;
        stream->ReadValue(dropped);
        stream->ReadValue(damage);
        kind = part >= 0 ? AIOptionKind::SpellAttack : AIOptionKind::Weapon;
        range = 1.0f;
        maxCount = 0;
        radius = 0.0f;
        effect = 0;
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
    for (std::uint32_t value : {std::bit_cast<std::uint32_t>(part), std::bit_cast<std::uint32_t>(range),
                                std::bit_cast<std::uint32_t>(maxCount), std::bit_cast<std::uint32_t>(radius),
                                std::bit_cast<std::uint32_t>(effect)})
        stream->WriteValue(value);
    stream->WriteValue(damage);
}

void AIAttention::Build(CUnit* unit) {
    auto* server = unit->ServerController();
    weapons.clear();
    spells.clear();
    if (!IsTextKey(unit->id)) {
        if (server->stats.naturalWeapon != kNaturalSpellsOnly) {
            float range = server->AttackRange();
            AIAttackOption option = MakeOption(AIOptionKind::Weapon, -1, range, 0, 0.0f, 0);
            server->GetDamageByType(&option.damage);
            weapons.push_back(option);
        }
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const CSpell* spell = unit->GetSpellObject(slot);
            if (!spell) continue;
            AIOptionKind kind = SpellKind(spell);
            if (kind == AIOptionKind::Unused) continue;
            float range = spell->range;
            if (server->stats.naturalWeapon == kNaturalSpellsOnly) range = server->stats.combat[UnitStats::kReach];
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
                effect = static_cast<int>(spell->Type());
                break;
            case AIOptionKind::Heal:
                radius = spell->damage;
                break;
            default:
                break;
            }
            switch (kind) {
            case AIOptionKind::Weapon:
            case AIOptionKind::SpellAttack:
            case AIOptionKind::AreaAttack:
            case AIOptionKind::LastingArea: {
                AIAttackOption option = MakeOption(kind, static_cast<int>(slot), range, maxCount, radius, effect);
                server->GetSpellDamage(slot, &option.damage);
                weapons.push_back(option);
                break;
            }
            case AIOptionKind::Curse:
                weapons.push_back(MakeOption(AIOptionKind::Curse, static_cast<int>(slot), range, maxCount, radius, effect));
                break;
            case AIOptionKind::Protect:
            case AIOptionKind::Enhance:
            case AIOptionKind::Heal:
                spells.push_back(MakeOption(kind, static_cast<int>(slot), range, maxCount, radius, effect));
                break;
            default:
                break;
            }
        }
    } else {
        float range = server->AttackRange();
        AIAttackOption option = MakeOption(AIOptionKind::Weapon, -1, range, 0, 0.0f, 0);
        server->GetDamageByType(&option.damage);
        weapons.push_back(option);
    }
    healWeight = 0.0f;
    for (int i = 0; i < std::ssize(weapons); ++i) {
        float weight = OptionWeight(&weapons[i]);
        healWeight = MaxKeepB(healWeight, weight);
    }
}

// The count, the array sized to it (SetSize inlined), then each option.
std::vector<AIAttackOption> LoadAttackOptions(ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    std::vector<AIAttackOption> options(count);
    for (AIAttackOption& option : options) option.Load(stream);
    return options;
}
