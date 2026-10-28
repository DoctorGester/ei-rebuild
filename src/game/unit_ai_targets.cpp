// The unit AI's target choice: scoring an attack option against a unit, whether an
// option may be used, choosing the best target, and the orders it checks
// and whether the unit can get at a target.
#include "sector_map.h"
#include "unit_ai.h"
#include "combat.h"

#include "ai_map.h"
#include "ai_scoped_path.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "spell.h"
#include "unit_db.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/x87.h"
#include "lib/mfc.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <bit>
#include <cmath>
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
constexpr float kOne = 1.0f;
constexpr float kZero = 0.0f;
constexpr float kHalf = 0.5f;
constexpr float kAllyNear2 = 49.0f;             // allies within 7 of an area target
constexpr float kPathNear = 30.0f;
constexpr int kNoHitsKnown = 20;
constexpr std::uint32_t kSpellNoTarget = 0x20000000;   // CSpell::targetFlags_: aimed at no object
constexpr int kRejectedReport = 0x14;
const float kNoScore = std::bit_cast<float>(0xF149F2CAu);    // -1e30
const float kFloorScore = std::bit_cast<float>(0xE0AD78ECu); // and the starting best: -1e20

const Vector3* PositionOf(const CUnit* unit) { return &unit->localPosition_; }

// The prototype's height (compared with 0.5: units below it count as small).
float PrototypeSize(CUnit* unit) {
    return std::bit_cast<float>(unit->GetPrototype()->height);
}

bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

std::uint32_t Excess(const AIAttackOption* option, const CUnit* target) {
    return option->damage.ExcessOver(&target->ServerController()->protection);
}

// Cases 2 and 3: the damage summed over the group members within the option's radius of the
// candidate (NaN distances count).
std::uint32_t GroupExcess(const AIAttackOption* option, AIObjectList* group, CUnit* candidate) {
    std::uint32_t sum = 0;
    if (!group->head) return sum;
    float radius2 = static_cast<float>(option->radius * option->radius);
    for (auto* node = group->head; node; node = node->next) {
        auto* member = node->data;
        EI_NAN_AUDIT(AIDistance2(PositionOf(member), PositionOf(candidate)), radius2);
        if (AIDistance2(PositionOf(member), PositionOf(candidate)) <= radius2) sum += Excess(option, member);
    }
    return sum;
}

// A scored {part, object} the choice keeps (CList nodes).
struct Scored {
    int part;
    CUnit* object;
    float score;
};
} // namespace

double __fastcall AIDistance2(const Vector3* a, const Vector3* b) {
    return DistanceSquaredXYUnrounded<double>(*a, *b);
}

void AIOrder::Clear() {
    kind = 9;
    if (target) target->Release();
    target = nullptr;
    flag18 = 0;
}

void AIOrder::MakeAttack(CMapObject* object) {
    Clear();
    kind = 3;
    source = 6;
    if (object) {
        ++object->refCount_;
        target = object;
    }
}

void AIOrder::MakeCast(int spell, CMapObject* object) {
    Clear();
    kind = 5;
    if (object) {
        ++object->refCount_;
        target = object;
    }
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
            PrototypeSize(target) >= kHalf)
            return false;
    }

    // Where the order is checked from: the unit's place, or the end of a path towards the target.
    Vector3 point;
    if (self == target || static_cast<char>(fromHere) != 0) {
        point = *PositionOf(self);
    } else {
        double dz = static_cast<double>(self->localPosition_.z) - target->localPosition_.z;
        double dy = static_cast<double>(self->localPosition_.y) - target->localPosition_.y;
        double dx = static_cast<double>(self->localPosition_.x) - target->localPosition_.x;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        float range = static_cast<float>(self->ScaledValue(distance));
        float targetRadius = static_cast<float>(target->Radius());
        float minDistance = static_cast<float>(static_cast<double>(self->Radius()) + targetRadius);
        CWorldServer* server = TheServerWorld();
        // The target is lifted off the sector map so it does not block its own cell.
        server->Sectors()->Remove(target);
        if (server->AIMap()->FindPath(self, PositionOf(target), &local.path, range, minDistance)) {
            float scale = static_cast<float>(kOne * kHalf);
            point.x = static_cast<float>(scale * local.path.targetX);
            point.y = static_cast<float>(scale * local.path.targetY);
            point.z = 0.0f;
        } else {
            point = *PositionOf(self);
        }
        server->Sectors()->Add(target);
    }

    AIOrder order;
    order.target = nullptr;
    order.Clear();
    if (part < 0) {
        order.MakeAttack(target);
    } else {
        order.MakeCast(part, target);
    }
    bool allowed = brain->owner->CanReachAndPlanFrom(&order, &point);
    order.target.Reset();
    return allowed;
}

const AIAttackOption* AIAttention::FindOption(int part) const {
    for (int i = 0; i < weapons.size; ++i)
        if (weapons.data[i].part == part) return &weapons.data[i];
    for (int i = 0; i < spells.size; ++i)
        if (spells.data[i].part == part) return &spells.data[i];
    return nullptr;
}

bool AIAttention::IsUsable(AIBrain* brain, const AITargetChoice* choice) {
    CUnit* target = choice->object;
    if (!target) return true;
    CUnit* self = brain->owner->unit;
    const AIAttackOption* option = FindOption(choice->part);
    if (!option) return false;
    if (option->kind < 2 || option->kind > 3) return true;
    EI_NAN_AUDIT(PrototypeSize(self));
    if (self->sizeClass == 0 && PrototypeSize(self) >= kHalf) return true;
    Vector3 at = *PositionOf(target);
    for (auto* node = brain->memory.nearby.head; node; node = node->next) {
        auto* other = node->data;
        double dy = static_cast<double>(at.y) - other->localPosition_.y;
        double dx = static_cast<double>(at.x) - other->localPosition_.x;
        EI_NAN_AUDIT(dx, dy, PrototypeSize(other));
        if (LengthSquared2DUnrounded(dx, dy) < kAllyNear2 && Allied(other->side, self->side)) {
            if (other->sizeClass != 0) return false;
            if (PrototypeSize(other) < kHalf) return false;
        }
    }
    double dy = static_cast<double>(self->localPosition_.y) - at.y;
    double dx = static_cast<double>(self->localPosition_.x) - at.x;
    double radius = option->radius;
    EI_NAN_AUDIT(radius, dx, dy);
    return radius * radius < LengthSquared2DUnrounded(dx, dy);
}

double AIAttention::Score(const Vector3* from, CUnit* self, AIObjectList* group, CEngineObject* current, CUnit* candidate,
                         float threshold, const AIAttackOption* option) {
    float score = candidate == current ? kCurrentBonus : kZero;
    CUnitServer* control = candidate->ServerController();
    int others = group->count - 1;
    int cap = option->maxCount;
    std::uint32_t damage = 0;   // only the low 16 bits count
    switch (option->kind) {
    case 0:
        damage = Excess(option, candidate);
        score = static_cast<float>(static_cast<double>(static_cast<int>(damage & 0xFFFF)) * kDamageWeight + score);
        break;
    case 1:
    case 2:
    case 3: {
        if (option->kind == 1) {
            damage = Excess(option, candidate);
        } else {
            if (option->kind == 3 && control->state == 0) return kNoScore;
            damage = GroupExcess(option, group, candidate);
            if (option->kind == 3) damage *= static_cast<std::uint32_t>(option->effect / 4 / 2);
            ++others;   // the group without the one left out
        }
        int words = static_cast<int>(damage & 0xFFFF);
        double partial = static_cast<double>(words) * kGroupDamageWeight + score + kGroupBase;
        int count = others < cap ? others : cap;
        score = static_cast<float>(static_cast<double>(words * count) + partial);
        break;
    }
    case 4:
    case 5:
    case 6: {
        if (candidate->HasEffect(static_cast<std::uint32_t>(option->effect))) return kNoScore;
        double partial = static_cast<double>(score) + kOne;
        int count = others < cap ? others : cap;
        score = static_cast<float>(static_cast<double>(count) + partial);
        break;
    }
    case 7: {
        float base = static_cast<float>(kGroupBase + static_cast<double>(score));
        int hp = candidate->packedVitals.hitPoints;
        long limit = x87::RoundToInt(static_cast<float>(control->stats.ReduceKind1(static_cast<float>(candidate->packedVitals.maxHitPoints))));
        if (hp > limit - 2) return kNoScore;
        if (hp * 3 < limit) {
            float weight = control->Brain()->attention.healWeight;
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
    double reach = kZero;
    float scale = kOne;
    if (option->kind < 0 || option->kind > 4) {
        reach = option->range;
        scale = 10.0f;
    }
    float beyond = static_cast<float>(std::sqrt(DistanceSquaredXYUnrounded<double>(candidate->localPosition_, *from)) - reach);
    float penalty = kZero >= beyond ? kZero : beyond;
    double result = static_cast<double>(score) - static_cast<double>(penalty) * kDistanceWeight * scale;
    EI_NAN_AUDIT(result);
    if (result < threshold) return result;
    if (option->kind >= 0 && option->kind <= 3) {
        int hits = (damage & 0xFFFF) == 0 ? kNoHitsKnown : candidate->packedVitals.hitPoints / static_cast<int>(damage & 0xFFFF);
        result -= static_cast<double>(hits) * kHitWeight;
    }
    return result;
}

void AIAttention::Choose(AIBrain* brain, AITargetChoice* choice, AIObjectList* candidates, const Vector3* from, bool alliesOnly,
                         int reachArg) {
    if (spells.size == 0 && (candidates->count == 0 || weapons.size == 0)) {
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
        if (candidates->count == 1 && spells.size == 0) {
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
        for (auto* node = candidates->head; node; node = node->next) {
            auto* candidate = node->data;
            for (int i = 0; i < weapons.size; ++i) {
                double s = Score(from, self, candidates, held, candidate, best, &weapons.data[i]);
                if (s > best) scored.push_back({weapons.data[i].part, candidate, static_cast<float>(s)});
            }
        }
    }
    if (spells.size != 0) {
        // The unit and its allies in sight that are alive and not reported.
        AIObjectList allies{};
        allies.vtable = mfc::impl::PtrListClass::Vtable();
        allies.blockSize = 10;
        allies.AddTail(self);
        for (auto* node = brain->memory.nearby.head; node; node = node->next) {
            auto* other = node->data;
            if (other->IsDead() == 0 && !brain->memory.reports.Contains(other) && Allied(self->side, other->side))
                allies.AddTail(other);
        }
        for (auto* node = allies.head; node; node = node->next) {
            auto* ally = node->data;
            for (int i = 0; i < spells.size; ++i) {
                const AIAttackOption* option = &spells.data[i];
                if (alliesOnly && option->kind == 6) continue;
                double s = Score(from, self, &allies, held, ally, best, option);
                if (s > best) scored.push_back({option->part, ally, static_cast<float>(s)});
            }
        }
        allies.RemoveAll();
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
            if (alliesOnly && !brain->IsNearPath(PositionOf(object), kPathNear)) {
                AIOrder order{};
                if (choice->part < 0) {
                    order.MakeAttack(object);
                } else {
                    order.MakeCast(choice->part, object);
                }
                allowed = brain->owner->CanReachAndPlanFrom(&order, PositionOf(self));
                order.target.Reset();
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
