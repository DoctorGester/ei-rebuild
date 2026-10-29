// The unit AI's target choice: scoring an attack option against a unit, whether an
// option may be used, choosing the best target, and the orders it checks
// and whether the unit can get at a target.
#include "ai_map.h"
#include "ai_scoped_path.h"
#include "combat.h"
#include "game_singletons.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "sector_map.h"
#include "spell.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cmath>
#include <cstdint>
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
constexpr std::uint32_t kSpellNoTarget = 0x20000000;   // CSpell::targetFlags_: aimed at no object
constexpr int kRejectedReport = 0x14;
constexpr float kNoScore = -1e30f;
static_assert(std::bit_cast<std::uint32_t>(kNoScore) == 0xF149F2CAu);
constexpr float kFloorScore = -1e20f; // and the starting best: -1e20
static_assert(std::bit_cast<std::uint32_t>(kFloorScore) == 0xE0AD78ECu);


// The prototype's height (compared with 0.5: units below it count as small).
float PrototypeSize(CUnit* unit) {
    return unit->GetPrototype()->height;
}


std::uint32_t Excess(const AIAttackOption* option, const CUnit* target) {
    return option->damage.ExcessOver(&target->ServerController()->protection);
}

// Cases 2 and 3: the damage summed over the group members within the option's radius of the
// candidate (NaN distances count).
std::uint32_t GroupExcess(const AIAttackOption* option, UnitList* group, CUnit* candidate) {
    std::uint32_t sum = 0;
    if (group->empty()) return sum;
    float radius2 = static_cast<float>(option->radius * option->radius);
    for (auto node = group->begin(); node != group->end(); ++node) {
        auto* member = *node;
        EI_NAN_AUDIT(AIDistance2(&member->localPosition_, &candidate->localPosition_), radius2);
        if (AIDistance2(&member->localPosition_, &candidate->localPosition_) <= radius2) sum += Excess(option, member);
    }
    return sum;
}

// A scored {part, object} the choice keeps (list nodes).
struct Scored {
    int part;
    CUnit* object;
    float score;
};
} // namespace

double AIDistance2(const Vector3* a, const Vector3* b) {
    return DistanceSquaredXYUnrounded<double>(*a, *b);
}

void AIOrder::Clear() {
    kind = UnitOrderKind::None;
    target.Reset();
    pointIsUseSpot = 0;
}

void AIOrder::MakeAttack(CMapObject* object) {
    Clear();
    kind = UnitOrderKind::Attack;
    source = 6;
    target.Assign(object);
}

void AIOrder::MakeCast(int spell, CMapObject* object) {
    Clear();
    kind = UnitOrderKind::CastOn;
    target.Assign(object);
    source = static_cast<std::uint32_t>(spell);
}

bool AIAttention::CanReach(AIBrain* brain, const AITargetChoice* choice, int fromHere) {
    ScopedPath local;
    CUnit* self = brain->owner->unit;
    CUnit* target = choice->object;
    int part = choice->part;
    // A spell aimed at no object cannot be cast on a large unit of size class 0.
    if (part >= 0) {
        CSpell* spell = self->GetSpellObject(part);
        if (spell && (spell->targetFlags_ & kSpellNoTarget) && target->sizeClass == 0 &&
            PrototypeSize(target) >= 0.5f)
            return false;
    }

    // Where the order is checked from: the unit's place, or the end of a path towards the target.
    Vector3 point;
    if (self == target || static_cast<char>(fromHere) != 0) {
        point = self->localPosition_;
    } else {
        double dz = static_cast<double>(self->localPosition_.z) - target->localPosition_.z;
        double dy = static_cast<double>(self->localPosition_.y) - target->localPosition_.y;
        double dx = static_cast<double>(self->localPosition_.x) - target->localPosition_.x;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        float range = static_cast<float>(self->ScaledValue(distance));
        float targetRadius = static_cast<float>(target->Radius());
        float minDistance = static_cast<float>(static_cast<double>(self->Radius()) + targetRadius);
        CWorldServer* server = &g_serverWorld.Get();
        // The target is lifted off the sector map so it does not block its own cell.
        server->aiMap_.sectorMap.Remove(target);
        if (server->aiMap_.FindPath(self, &target->localPosition_, &local.path, range, minDistance)) {
            float scale = 0.5f;
            point.x = static_cast<float>(scale * local.path.targetX);
            point.y = static_cast<float>(scale * local.path.targetY);
            point.z = 0.0f;
        } else {
            point = self->localPosition_;
        }
        server->aiMap_.sectorMap.Add(target);
    }

    AIOrder order;
    order.Clear();
    if (part < 0) {
        order.MakeAttack(target);
    } else {
        order.MakeCast(part, target);
    }
    return brain->owner->CanReachAndPlanFrom(&order, &point);
}

const AIAttackOption* AIAttention::FindOption(int part) const {
    for (int i = 0; i < std::ssize(weapons); ++i)
        if (weapons[i].part == part) return &weapons[i];
    for (int i = 0; i < std::ssize(spells); ++i)
        if (spells[i].part == part) return &spells[i];
    return nullptr;
}

bool AIAttention::IsUsable(AIBrain* brain, const AITargetChoice* choice) {
    CUnit* target = choice->object;
    if (!target) return true;
    CUnit* self = brain->owner->unit;
    const AIAttackOption* option = FindOption(choice->part);
    if (!option) return false;
    if (option->kind != AIOptionKind::AreaAttack && option->kind != AIOptionKind::LastingArea) return true;
    EI_NAN_AUDIT(PrototypeSize(self));
    if (self->sizeClass == 0 && PrototypeSize(self) >= 0.5f) return true;
    Vector3 at = target->localPosition_;
    for (auto& node : brain->memory.nearby) {
        CUnit* other = node;
        double dy = static_cast<double>(at.y) - other->localPosition_.y;
        double dx = static_cast<double>(at.x) - other->localPosition_.x;
        EI_NAN_AUDIT(dx, dy, PrototypeSize(other));
        if (LengthSquared2DUnrounded(dx, dy) < kAllyNear2 && g_serverWorld->diplomacy_.IsAllied(other->side, self->side)) {
            if (other->sizeClass != 0) return false;
            if (PrototypeSize(other) < 0.5f) return false;
        }
    }
    double dy = static_cast<double>(self->localPosition_.y) - at.y;
    double dx = static_cast<double>(self->localPosition_.x) - at.x;
    double radius = option->radius;
    EI_NAN_AUDIT(radius, dx, dy);
    return radius * radius < LengthSquared2DUnrounded(dx, dy);
}

double AIAttention::Score(const Vector3* from, CUnit* self, UnitList* group, CEngineObject* current, CUnit* candidate,
                         float threshold, const AIAttackOption* option) {
    float score = candidate == current ? kCurrentBonus : 0.0f;
    CUnitServer* control = candidate->ServerController();
    int others = std::ssize(*group) - 1;
    int cap = option->maxCount;
    std::uint32_t damage = 0;   // only the low 16 bits count
    switch (option->kind) {
    case AIOptionKind::Weapon:
        damage = Excess(option, candidate);
        score = static_cast<float>(static_cast<double>(static_cast<int>(damage & 0xFFFF)) * kDamageWeight + score);
        break;
    case AIOptionKind::SpellAttack:
    case AIOptionKind::AreaAttack:
    case AIOptionKind::LastingArea: {
        if (option->kind == AIOptionKind::SpellAttack) {
            damage = Excess(option, candidate);
        } else {
            if (option->kind == AIOptionKind::LastingArea && control->state == 0) return kNoScore;
            damage = GroupExcess(option, group, candidate);
            if (option->kind == AIOptionKind::LastingArea) damage *= static_cast<std::uint32_t>(option->effect / 4 / 2);
            ++others;   // the group without the one left out
        }
        int words = static_cast<int>(damage & 0xFFFF);
        double partial = static_cast<double>(words) * kGroupDamageWeight + score + kGroupBase;
        int count = others < cap ? others : cap;
        score = static_cast<float>(static_cast<double>(words * count) + partial);
        break;
    }
    case AIOptionKind::Curse:
    case AIOptionKind::Protect:
    case AIOptionKind::Enhance: {
        if (candidate->HasEffect(static_cast<std::uint32_t>(option->effect))) return kNoScore;
        double partial = static_cast<double>(score) + 1.0f;
        int count = others < cap ? others : cap;
        score = static_cast<float>(static_cast<double>(count) + partial);
        break;
    }
    case AIOptionKind::Heal: {
        float base = static_cast<float>(kGroupBase + static_cast<double>(score));
        int hp = candidate->packedVitals.hitPoints;
        long limit = x87::RoundToInt(static_cast<float>(control->stats.ReduceKind1(static_cast<float>(candidate->packedVitals.maxHitPoints))));
        if (hp > limit - 2) return kNoScore;
        if (hp * 3 < limit) {
            float weight = control->brain.attention.healWeight;
            score = static_cast<float>(kHealFactor * weight);
            if (self->packedVitals.hitPoints == self->packedVitals.maxHitPoints) score = static_cast<float>(static_cast<double>(score) + weight);
        } else {
            double partial = static_cast<double>(base) + kHealGroupBase;
            int count = others < cap ? others : cap;
            score = static_cast<float>(static_cast<double>(count * 2) + partial);
        }
        break;
    }
    default:
        break;
    }
    EI_NAN_AUDIT(score, threshold);
    if (score < threshold) return score;
    double reach = 0.0;
    float scale = 1.0f;
    if (option->kind < AIOptionKind::Weapon || option->kind > AIOptionKind::Curse) {
        reach = option->range;
        scale = 10.0f;
    }
    float beyond = static_cast<float>(std::sqrt(DistanceSquaredXYUnrounded<double>(candidate->localPosition_, *from)) - reach);
    float penalty = 0.0f >= beyond ? 0.0f : beyond;
    double result = static_cast<double>(score) - static_cast<double>(penalty) * kDistanceWeight * scale;
    EI_NAN_AUDIT(result);
    if (result < threshold) return result;
    if (option->kind >= AIOptionKind::Weapon && option->kind <= AIOptionKind::LastingArea) {
        int hits = (damage & 0xFFFF) == 0 ? kNoHitsKnown : candidate->packedVitals.hitPoints / static_cast<int>(damage & 0xFFFF);
        result -= static_cast<double>(hits) * kHitWeight;
    }
    return result;
}

void AIAttention::Choose(AIBrain* brain, AITargetChoice* choice, UnitList* candidates, const Vector3* from, bool alliesOnly,
                         int reachArg) {
    if (std::ssize(spells) == 0 && (candidates->empty() || std::ssize(weapons) == 0)) {
        choice->object = nullptr;
        return;
    }
    AITargetChoice current{-1, nullptr};
    brain->attention.CurrentAttack(brain, &current);
    if (!IsUsable(brain, &current)) current.object = nullptr;
    CUnit* held = current.object;
    float best = kFloorScore;
    CUnit* self = brain->owner->unit;
    if (held) {
        if (std::ssize(*candidates) == 1 && std::ssize(spells) == 0) {
            *choice = current;
            return;
        }
        const AIAttackOption* option = FindOption(current.part);
        if (!option) {
            held = nullptr;
            current.object = nullptr;
        } else {
            best = static_cast<float>(Score(from, self, candidates, held, held, kFloorScore, option));
            EI_NAN_AUDIT(best);
            if (best < kFloorScore) {
                current.object = nullptr;
                best = kFloorScore;
            }
        }
    }

    std::vector<Scored> scored;
    if (!alliesOnly) {
        for (auto node = candidates->begin(); node != candidates->end(); ++node) {
            auto* candidate = *node;
            for (int i = 0; i < std::ssize(weapons); ++i) {
                double s = Score(from, self, candidates, held, candidate, best, &weapons[i]);
                if (s > best) scored.push_back({weapons[i].part, candidate, static_cast<float>(s)});
            }
        }
    }
    if (std::ssize(spells) != 0) {
        // The unit and its allies in sight that are alive and not reported.
        UnitList allies{};
        allies.push_back(self);
        for (auto& node : brain->memory.nearby) {
            CUnit* other = node;
            if (other->IsDead() == 0 && !brain->memory.reports.Contains(other) && g_serverWorld->diplomacy_.IsAllied(self->side, other->side))
                allies.push_back(other);
        }
        for (auto& node : allies) {
            auto* ally = node;
            for (int i = 0; i < std::ssize(spells); ++i) {
                const AIAttackOption* option = &spells[i];
                if (alliesOnly && option->kind == AIOptionKind::Enhance) continue;
                double s = Score(from, self, &allies, held, ally, best, option);
                if (s > best) scored.push_back({option->part, ally, static_cast<float>(s)});
            }
        }
        allies.clear();
    }

    while (!scored.empty()) {
        *choice = current;
        int chosen = -1;
        float top = kNoScore;
        for (int i = 0; i < static_cast<int>(scored.size()); ++i) {
            EI_NAN_AUDIT(top, scored[i].score);
            if (top < scored[i].score) {
                choice->part = scored[i].part;
                choice->object = scored[i].object;
                chosen = i;
                top = scored[i].score;
            }
        }
        if (chosen < 0) return;
        if (choice->object == current.object && choice->part == current.part) return;
        CUnit* object = choice->object;
        if (IsUsable(brain, choice)) {
            bool allowed = true;
            if (alliesOnly && !brain->IsNearPath(&object->localPosition_, kPathNear)) {
                AIOrder order{};
                if (choice->part < 0) {
                    order.MakeAttack(object);
                } else {
                    order.MakeCast(choice->part, object);
                }
                allowed = brain->owner->CanReachAndPlanFrom(&order, &self->localPosition_);
            }
            if (allowed && CanReach(brain, choice, reachArg)) return;
        }
        // Rejected: an object scored only once (and not the current target) is reported.
        int seen = 0;
        for (const Scored& entry : scored)
            if (entry.object == object) ++seen;
        if (seen == 1 && choice->object != current.object) brain->memory.reports.Report(object, kRejectedReport);
        scored.erase(scored.begin() + chosen);
    }
    *choice = current;
}
