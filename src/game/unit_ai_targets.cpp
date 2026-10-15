// The unit AI's target choice: scoring an attack option against a unit (005CF9A0), whether an
// option may be used (005CF7F0), choosing the best target (005D0030), and the orders it checks
// (00545F40, 005CF720, 005CF780), and whether the unit can get at a target (005CF3C0).
#include "unit_ai.h"

#include "ai_map.h"
#include "ai_scoped_path.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "spell.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <bit>
#include <cmath>
#include <vector>

namespace {
constexpr float kCurrentBonus = 10.0f;          // 0073FF04
constexpr float kGroupBase = 3.0f;              // 0073FF08
constexpr float kDamageWeight = 1.0f;           // 0073FF0C
constexpr float kGroupDamageWeight = 1.3f;      // 0073FF10 (0x3FA66666)
constexpr float kHealFactor = 3.0f;             // 0073FF14
constexpr float kDistanceWeight = 25.0f;        // 0073FF18
constexpr float kHitWeight = 2.0f;              // 0073FF1C
constexpr float kHealGroupBase = 2.0f;          // 0073CDB8
constexpr float kOne = 1.0f;                    // 0073B84C
constexpr float kZero = 0.0f;                   // 0073B858
constexpr float kHalf = 0.5f;                   // 0073B86C
constexpr float kAllyNear2 = 49.0f;             // 0073FF28: allies within 7 of an area target
constexpr float kPathNear = 30.0f;              // 0073FEFC
constexpr int kNoHitsKnown = 20;
constexpr std::uint32_t kSpellNoTarget = 0x20000000;   // CSpell::targetFlags_: aimed at no object
constexpr int kRejectedReport = 0x14;
const float kNoScore = std::bit_cast<float>(0xF149F2CAu);    // 0073FF2C: -1e30
const float kFloorScore = std::bit_cast<float>(0xE0AD78ECu); // 0073FF30 and the starting best: -1e20

CMapObjectVirtuals* MapObject(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object); }
UnitController* ControllerOf(const CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }
const Vector3* PositionOf(const CUnit* unit) { return &unit->position; }

// The prototype's +0x24 (compared with 0.5: units below it count as small).
float PrototypeSize(CUnit* unit) {
    return std::bit_cast<float>(reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype())->height);
}

bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

std::uint32_t Excess(const AIAttackOption* option, const CUnit* target) {
    return ExcessDamageOriginal(option->damage, nullptr, ControllerOf(target)->protection);
}

// Cases 2 and 3: the damage summed over the group members within the option's radius of the
// candidate (NaN distances count).
std::uint32_t GroupExcess(const AIAttackOption* option, AIObjectList* group, CUnit* candidate) {
    std::uint32_t sum = 0;
    if (!group->head) return sum;
    float radius2 = RoundToFloat(option->radius * option->radius);
    for (auto* node = group->head; node; node = node->next) {
        auto* member = static_cast<CUnit*>(node->data);
        if (!(AIDistance2(PositionOf(member), PositionOf(candidate)) > radius2)) sum += Excess(option, member);
    }
    return sum;
}

// A scored {part, object} the choice keeps (the original's CList nodes, 0x14 bytes).
struct Scored {
    int part;
    CEngineObject* object;
    float score;
};
} // namespace

double __fastcall AIDistance2(const Vector3* a, const Vector3* b) {
    return DistanceSquaredXYUnrounded<double>(*a, *b);
}
EI_REPLACE(0x005CFE20, AIDistance2);

void AIOrder::Clear() {
    kind = 9;
    if (object) object->Release();
    object = nullptr;
    flag = 0;
}
EI_REPLACE_METHOD(0x00545F40, AIOrder::Clear);

void AIOrder::MakeAttack(CEngineObject* target) {
    Clear();
    kind = 3;
    what = 6;
    if (target) {
        ++target->refCount_;
        object = target;
    }
}
EI_REPLACE_METHOD(0x005CF720, AIOrder::MakeAttack);

void AIOrder::MakeCast(int spell, CEngineObject* target) {
    Clear();
    kind = 5;
    if (target) {
        ++target->refCount_;
        object = target;
    }
    what = spell;
}
EI_REPLACE_METHOD(0x005CF780, AIOrder::MakeCast);

// 005CF3C0
bool AIAttention::CanReach(AIBrain* brain, const int* partAndObject, int fromHere) {
    ScopedPath local;
    CUnit* self = brain->owner->unit;
    auto* target = reinterpret_cast<CUnit*>(partAndObject[1]);
    int part = partAndObject[0];
    // A spell aimed at no object cannot be cast on a large unit of size class 0.
    if (part >= 0) {
        auto* spell = static_cast<CSpell*>(self->GetSpellObject(part));
        if (spell && (spell->targetFlags_ & kSpellNoTarget) && target->sizeClass == 0 &&
            PrototypeSize(target) >= kHalf)
            return false;
    }

    // Where the order is checked from: the unit's place, or the end of a path towards the target.
    Vector3 point;
    if (self == target || static_cast<char>(fromHere) != 0) {
        point = *PositionOf(self);
    } else {
        double dz = static_cast<double>(self->position.z) - target->position.z;
        double dy = static_cast<double>(self->position.y) - target->position.y;
        double dx = static_cast<double>(self->position.x) - target->position.x;
        float distance = RoundToFloat(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        float range = RoundToFloat(self->ScaledValue(distance));
        float targetRadius = RoundToFloat(MapObject(target)->Radius());
        float minDistance = RoundToFloat(static_cast<double>(MapObject(self)->Radius()) + targetRadius);
        CWorldServer* server = WorldServer();
        // The target is lifted off the sector map so it does not block its own cell.
        SectorMapRemove(server->Sectors(), nullptr, target);
        if (server->AIMap()->FindPath(self, PositionOf(target), &local.path, range, minDistance)) {
            float scale = RoundToFloat(kOne * kHalf);
            point.x = RoundToFloat(scale * local.path.targetX);
            point.y = RoundToFloat(scale * local.path.targetY);
            point.z = 0.0f;
        } else {
            point = *PositionOf(self);
        }
        SectorMapAdd(server->Sectors(), nullptr, target);
    }

    AIOrder order;
    order.object = nullptr;
    order.Clear();
    if (part < 0) {
        order.MakeAttack(target);
    } else {
        order.MakeCast(part, target);
    }
    bool allowed = brain->owner->CheckOrderAt(&order, &point);
    reinterpret_cast<AIRef*>(&order.object)->Reset();
    return allowed;
}
EI_REPLACE_METHOD(0x005CF3C0, AIAttention::CanReach);

const AIAttackOption* AIAttention::FindOption(int part) const {
    for (int i = 0; i < weapons.size; ++i)
        if (weapons.data[i].part == part) return &weapons.data[i];
    for (int i = 0; i < spells.size; ++i)
        if (spells.data[i].part == part) return &spells.data[i];
    return nullptr;
}

bool AIAttention::IsUsable(AIBrain* brain, const int* partAndObject) {
    auto* target = reinterpret_cast<CUnit*>(partAndObject[1]);
    if (!target) return true;
    CUnit* self = brain->owner->unit;
    const AIAttackOption* option = FindOption(partAndObject[0]);
    if (!option) return false;
    if (option->kind < 2 || option->kind > 3) return true;
    if (self->sizeClass == 0 && !(PrototypeSize(self) < kHalf)) return true;
    Vector3 at = *PositionOf(target);
    for (auto* node = brain->nearby.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        double dy = static_cast<double>(at.y) - other->position.y;
        double dx = static_cast<double>(at.x) - other->position.x;
        if (!(LengthSquared2DUnrounded(dx, dy) >= kAllyNear2) && Allied(other->side, self->side)) {
            if (other->sizeClass != 0) return false;
            if (!(PrototypeSize(other) >= kHalf)) return false;
        }
    }
    double dy = static_cast<double>(self->position.y) - at.y;
    double dx = static_cast<double>(self->position.x) - at.x;
    double radius = option->radius;
    return !(radius * radius >= LengthSquared2DUnrounded(dx, dy));
}

EI_REPLACE_METHOD(0x005CF7F0, AIAttention::IsUsable);

double AIAttention::Score(const Vector3* from, CUnit* self, AIObjectList* group, CEngineObject* current, CUnit* candidate,
                         float threshold, const AIAttackOption* option) {
    float score = static_cast<CEngineObject*>(candidate) == current ? kCurrentBonus : kZero;
    UnitController* control = ControllerOf(candidate);
    int others = group->count - 1;
    int cap = option->maxCount;
    std::uint32_t damage = 0;   // only the low 16 bits count
    switch (option->kind) {
    case 0:
        damage = Excess(option, candidate);
        score = RoundToFloat(static_cast<double>(static_cast<int>(damage & 0xFFFF)) * kDamageWeight + score);
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
        score = RoundToFloat(static_cast<double>(words * count) + partial);
        break;
    }
    case 4:
    case 5:
    case 6: {
        if (candidate->HasEffect(static_cast<std::uint32_t>(option->effect))) return kNoScore;
        double partial = static_cast<double>(score) + kOne;
        int count = others < cap ? others : cap;
        score = RoundToFloat(static_cast<double>(count) + partial);
        break;
    }
    case 7: {
        float base = RoundToFloat(kGroupBase + static_cast<double>(score));
        int hp = candidate->packedVitals.hitPoints;
        long limit = std::lrint(RoundToFloat(control->stats.ReduceKind1(static_cast<float>(candidate->packedVitals.maxHitPoints))));
        if (hp > limit - 2) return kNoScore;
        if (hp * 3 < limit) {
            float weight = control->Brain()->attention.healWeight;
            score = RoundToFloat(kHealFactor * weight);
            if (self->packedVitals.hitPoints == self->packedVitals.maxHitPoints) score = RoundToFloat(static_cast<double>(score) + weight);
        } else {
            double partial = static_cast<double>(base) + kHealGroupBase;
            int count = others < cap ? others : cap;
            score = RoundToFloat(static_cast<double>(count * 2) + partial);
        }
        break;
    }
    default:
        break;
    }
    if (!(score >= threshold)) return score;
    double reach = kZero;
    float scale = kOne;
    if (option->kind < 0 || option->kind > 4) {
        reach = option->range;
        scale = 10.0f;
    }
    float beyond = RoundToFloat(x87::fsqrt(DistanceSquaredXYUnrounded<double>(candidate->position, *from)) - reach);
    float penalty = !(kZero >= beyond) ? beyond : kZero;
    double result = static_cast<double>(score) - static_cast<double>(penalty) * kDistanceWeight * scale;
    if (!(result >= threshold)) return result;
    if (option->kind >= 0 && option->kind <= 3) {
        int hits = (damage & 0xFFFF) == 0 ? kNoHitsKnown : candidate->packedVitals.hitPoints / static_cast<int>(damage & 0xFFFF);
        result -= static_cast<double>(hits) * kHitWeight;
    }
    return result;
}
EI_REPLACE_METHOD(0x005CF9A0, AIAttention::Score);

void AIAttention::Choose(AIBrain* brain, int* partAndObject, AIObjectList* candidates, const Vector3* from, bool alliesOnly,
                         int reachArg) {
    if (spells.size == 0 && (candidates->count == 0 || weapons.size == 0)) {
        partAndObject[1] = 0;
        return;
    }
    int current[2] = {-1, 0};   // {part, object}
    brain->CurrentAttack(current);
    if (!IsUsable(brain, current)) current[1] = 0;
    auto* held = reinterpret_cast<CEngineObject*>(current[1]);
    float best = kFloorScore;
    CUnit* self = brain->owner->unit;
    if (held) {
        if (candidates->count == 1 && spells.size == 0) {
            partAndObject[0] = current[0];
            partAndObject[1] = current[1];
            return;
        }
        const AIAttackOption* option = FindOption(current[0]);
        if (!option) {
            held = nullptr;
            current[1] = 0;
        } else {
            best = RoundToFloat(Score(from, self, candidates, held, static_cast<CUnit*>(held), kFloorScore, option));
            if (!(best >= kFloorScore)) {
                current[1] = 0;
                best = kFloorScore;
            }
        }
    }

    std::vector<Scored> scored;
    if (!alliesOnly) {
        for (auto* node = candidates->head; node; node = node->next) {
            auto* candidate = static_cast<CUnit*>(node->data);
            for (int i = 0; i < weapons.size; ++i) {
                double s = Score(from, self, candidates, held, candidate, best, &weapons.data[i]);
                if (s > best) scored.push_back({weapons.data[i].part, candidate, RoundToFloat(s)});
            }
        }
    }
    if (spells.size != 0) {
        // The unit and its allies in sight that are alive and not reported.
        AIObjectList allies{};
        allies.vtable = mfc::PtrListVtable();
        allies.blockSize = 10;
        allies.AddTail(self);
        for (auto* node = brain->nearby.head; node; node = node->next) {
            auto* other = static_cast<CUnit*>(node->data);
            if (MapObject(other)->IsDead() == 0 && !ReportsContains(&brain->reports, nullptr, other) && Allied(self->side, other->side))
                allies.AddTail(other);
        }
        for (auto* node = allies.head; node; node = node->next) {
            auto* ally = static_cast<CUnit*>(node->data);
            for (int i = 0; i < spells.size; ++i) {
                const AIAttackOption* option = &spells.data[i];
                if (alliesOnly && option->kind == 6) continue;
                double s = Score(from, self, &allies, held, ally, best, option);
                if (s > best) scored.push_back({option->part, ally, RoundToFloat(s)});
            }
        }
        allies.RemoveAll();
    }

    while (!scored.empty()) {
        partAndObject[0] = current[0];
        partAndObject[1] = current[1];
        int chosen = -1;
        float top = kNoScore;
        for (int i = 0; i < static_cast<int>(scored.size()); ++i) {
            if (!(top >= scored[i].score)) {
                partAndObject[0] = scored[i].part;
                partAndObject[1] = reinterpret_cast<int>(scored[i].object);
                chosen = i;
                top = scored[i].score;
            }
        }
        if (chosen < 0) return;
        if (partAndObject[1] == current[1] && partAndObject[0] == current[0]) return;
        auto* object = reinterpret_cast<CEngineObject*>(partAndObject[1]);
        if (IsUsable(brain, partAndObject)) {
            bool allowed = true;
            if (alliesOnly && !brain->IsNearPath(PositionOf(static_cast<CUnit*>(object)), kPathNear)) {
                AIOrder order{};
                if (partAndObject[0] < 0) {
                    order.MakeAttack(object);
                } else {
                    order.MakeCast(partAndObject[0], object);
                }
                allowed = brain->owner->CheckOrderAt(&order, PositionOf(self));
                reinterpret_cast<AIRef*>(&order.object)->Reset();
            }
            if (allowed && CanReach(brain, partAndObject, reachArg)) return;
        }
        // Rejected: an object scored only once (and not the current target) is reported.
        int seen = 0;
        for (const Scored& entry : scored)
            if (entry.object == object) ++seen;
        if (seen == 1 && partAndObject[1] != current[1]) ReportsAdd(&brain->reports, nullptr, object, kRejectedReport);
        scored.erase(scored.begin() + chosen);
    }
    partAndObject[0] = current[0];
    partAndObject[1] = current[1];
}

EI_REPLACE_METHOD(0x005D0030, AIAttention::Choose);
