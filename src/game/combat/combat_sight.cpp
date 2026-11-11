// Combat, third part: whether an order can be carried out from where the unit stands (reach, units
// in the way, line of sight) and the planner that finds the moment it can. See combat.h,
// combat/combat_internal.h and docs/notes/combat.md ("Order lifecycle").
#include "game/combat/combat.h"

#include "game/ai/ai_map.h"
#include "game/ai/unit_ai.h"
#include "game/combat/combat_internal.h"
#include "engine/core/core_maths.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kPointOne = 0.1f;
constexpr float kPointThree = 0.3f;
constexpr float kMinusPointThree = -0.3f;
constexpr float kPointNine = 0.9f;
constexpr float kRangeSlack = 1.05f;
constexpr float kMeleeReach = 3.0f;
constexpr float kFifteen = 15.0f;
constexpr float kPercent = 0.01f;
constexpr float kNearSq = 10.0f;
constexpr float kFrontCosine = 0.5f;
constexpr float kUseSpotRange = 1e10f;     // point orders with pointIsUseSpot
constexpr float kBestStart = 1e30f;

// a > b (false for a NaN).
bool Greater(float a, float b) { return a > b; }

bool IsUnit(EngineObject* object) { return object && object->GetClassId() == ClassId::Unit; }

// The missile spell's delay in ticks: _ftol(sqrt(distanceSq) * 15 / flight - 3).
int CastDelayTicks(float distanceSq, float flight) {
    return lib::Truncate(std::sqrt(distanceSq) * 15.0f / flight - 3.0f);
}

// AI units only act on a target within 1.05 times their sight range: the larger of a stats value and the
// movement factor times the sight; the original's select picks the stats value for a NaN product.
bool OutOfSightRange(UnitServer* self, const Vector3& point) {
    float limit = self->stats.perception[Sense::Near].current;
    float range = self->moveFactors[MoveFactor::Sight] * self->stats.perception[Sense::Sight].current;
    float rangeValue = range;
    float chosen = MaxKeepB(rangeValue, limit);
    float reach = chosen * kRangeSlack;
    return reach * reach < DistanceSquaredXY(self->unit->localPosition, point);
}

// A melee or skill order fails when a living unit other than the target stands between the unit
// and `point` at `time`: 0.1..0.9 of the way along it and less than 0.3 to the side. `u` is the way divided by its
// squared length, so "along" is the fraction of the way; `v` is a unit vector, so "across" is in world units.
bool SomeoneInTheWay(UnitServer* self, const AIOrder* order, const Vector3& point, int time) {
    Unit* unit = self->unit;
    Vector3 d = point - unit->localPosition;
    Vector3 u = d / LengthSquared(d);
    float side = Length2D(XY(u));
    Vector3 v = Vector3{u.y, -u.x, 0.0f} / side;

    // The units the brain lists as nearby.
    for (auto node = self->brain.memory.nearby.begin(); node != self->brain.memory.nearby.end();) {
        Unit* other = *node;
        ++node;
        if (other == order->target) continue;
        if (other->IsDead() != 0) continue;
        Vector3 offset = other->PositionAt(time) - unit->localPosition;
        float along = Dot(offset, u);
        float across = Dot(offset, v);
        if (Greater(along, kPointOne) && along < kPointNine && Greater(across, kMinusPointThree) &&
            across < kPointThree)
            return true;
    }
    return false;
}

// Does the order need the way to its target clear of units (melee or skill)?
bool NeedsClearWay(UnitServer* self, const AIOrder* order) {
    if (order->kind == UnitOrderKind::Use) return true;
    if (order->kind != UnitOrderKind::Attack) return false;
    ItemWeapon* weapon = self->unit->ActiveWeapon();
    if (weapon && weapon->IsRanged()) return false;
    float reach = weapon ? weapon->reach : self->stats.combat[UnitStats::CombatIndex::Reach];
    return reach < kMeleeReach;
}

// The quick item or spell value the cast animation is built with: the spell's castTime scaled by
// 15 / stats.rate (unscaled when that is 0), rounded.
int CastValue(UnitServer* self, Spell* spell) {
    float scale = self->stats.rate;
    if (scale != 0.0f) scale = kFifteen / scale;   // a NaN stays the same NaN
    float value = static_cast<float>(spell->castTime) * scale;
    return lib::RoundEven(value);
}

void FillCastKind(OrderPlan* plan, Spell* spell) {
    plan->isCast = 1;
    if (!spell) {
        plan->castKind = SpellSchool::None;
        return;
    }
    if (spell->targetFlags & SpellTargetFlag::AtPoint) plan->targetId = -1;
    const itemdb::SpellRecord* record = spell->GetPrototype();
    plan->castKind = record->castSchool != SpellSchool::None ? record->castSchool : record->school;
}

constexpr float kAboveSurface = 1.0f;
constexpr float kMinSight = 0.1f;

} // namespace

// --- What must be seen -----------------------------------------------------------------------

bool UnitServer::FindSightTarget(const AIOrder* order, SightTarget* out) {
    if (order->pointIsUseSpot) return false;
    WorldServer* world = ServerWorld();
    MapObject* target = order->target;

    if (order->kind == UnitOrderKind::CastAt || order->kind == UnitOrderKind::CastOn) {
        Spell* spell = SpellOfSource(order->source);
        if (!spell) return false;
        if (spell->GetPrototype()->needsSight == 0) return false;
        out->point = {};
        out->minDistanceSq = 0.0f;
        out->unit = nullptr;
        if (Unit* other = UnitOrNull(target)) {
            out->unit = other;
            float reach = unit->standingHeight + other->standingHeight + kPointThree;
            out->minDistanceSq = reach * reach;
            out->eyeHeight = unit->BaseSpeed();
            return true;
        }
        out->point = order->point;
        out->point.z = world->SurfaceHeightAt(out->point.x, out->point.y) + 1.0f;
        out->eyeHeight = unit->BaseSpeed();
        return true;
    }

    Unit* targetUnit = UnitOrNull(target);
    if (!targetUnit) return false;
    out->point = {};
    out->minDistanceSq = kNearSq;
    out->unit = targetUnit;
    out->eyeHeight = unit->BaseSpeed();
    return true;
}

// --- Can the order be carried out from here ----------------------------------------------------

bool UnitServer::CanActFrom(const AIOrder* order, const Vector3& point, int time) {
    if (order->target == unit) return true;
    if (order->pointIsUseSpot) return true;
    if (!unit->player && order->target && OutOfSightRange(this, point)) return false;

    if (order->kind == UnitOrderKind::CastAt || order->kind == UnitOrderKind::CastOn) {
        if (unit->flags & MapObjectFlag::NoCasting) return false;
    } else if (NeedsClearWay(this, order) && SomeoneInTheWay(this, order, point, time)) {
        return false;
    }

    SightTarget sight;
    if (!FindSightTarget(order, &sight)) return true;
    if (sight.unit) {
        // A unit target is seen at `point`, at its own height above the ground there.
        sight.point = point;
        float ground = sight.unit->GroundHeight(point.x, point.y);
        sight.point.z = sight.unit->ModeSpeed() + ground;
        sight.unit = nullptr;
        // (The original then asks a remaining sight.unit for a virtual; it is always null here.)
    }
    float apartSq = DistanceSquaredXY(sight.point, unit->localPosition);
    if (apartSq < sight.minDistanceSq) return true;

    Vector3 eye = unit->localPosition;
    eye.z = unit->GroundHeight(eye.x, eye.y) + sight.eyeHeight;
    AIMap* map = &ServerWorld()->aiMap;
    float visibility = map->LineOfSight(eye, sight.point);
    return visibility >= kPointOne;
}

// --- The planner -----------------------------------------------------------------------------

bool UnitServer::PlanAction(const AIOrder* order, OrderPlan* plan) {
    AnimationSteps steps;
    const Vector3 origin = unit->localPosition;
    int now = OwningWorld()->tick;
    Spell* spell = nullptr;

    if (order->pointIsUseSpot) {
        float distance = ActionDistance(order);
        float limitSq = distance * distance;
        float apartSq = DistanceSquaredXY(order->point, unit->localPosition);
        if (limitSq < apartSq) return false;
    }

    plan->Reset();

    // The animation and its duration (the builders always set it; the original's is not initialised).
    int duration = 0;
    UnitOrderKind kind = order->kind;
    if (kind == UnitOrderKind::Attack) {
        BuildAttackSteps(plan, &steps, &duration, order->target);
    } else if (kind < UnitOrderKind::CastAt || kind > UnitOrderKind::CastOn) {
        BuildOrderSteps(&steps, &duration, kind, order->source);
    } else {
        spell = SpellOfSource(order->source);
        int value = 0;
        if (spell) value = CastValue(this, spell);
        BuildCastSteps(&steps, &duration, order->kind, value);
    }
    if (duration <= 0) duration = 1;

    float range = ActionDistance(order);
    if (order->pointIsUseSpot) range = kUseSpotRange;

    // The unit's heading and how many ticks a half turn takes.
    Vector3 axis = unit->localRotation.AxisY();
    Vector3 facing = -axis;
    float heading = lib::Atan2(facing.y, facing.x);
    const float turnSpeed = unit->GetMoveSpeeds().turn;
    float halfTurn = kPi / turnSpeed;
    int ticks = lib::RoundEven(halfTurn) + 3;

    bool predict = false;
    Unit* targetUnit = nullptr;
    int castDelay = 0;

    Vector3 aim = order->AimPoint();
    Vector3 delta = aim - origin;
    float distanceSq = DistanceSquaredXY(origin, aim);

    float reachSq;    // the order's own range, squared
    float outerSq;    // how far the target may be and still be waited for
    targetUnit = UnitOrNull(order->target);
    if (!targetUnit) {
        reachSq = range * range;
        outerSq = reachSq;
    } else {
        float speed = targetUnit->MoveSpeed();   // Unit::MoveSpeed: a float
        reachSq = range * range;
        bool inFront = false;
        if (!Greater(distanceSq, reachSq)) {
            float dot = Dot(delta, facing);
            float cone = std::sqrt(distanceSq) * kFrontCosine;
            inFront = cone < dot;
        }
        if (inFront) {
            // Within reach and in front: the target may walk off for half the turn and the action.
            int wait = ticks / 2 + duration;
            // The int halved rounds once: as the int rounded to a float, halved.
            float outer = speed * (static_cast<float>(wait) * 0.5f);
            outer += range;
            outer += 0.5f;
            outerSq = outer * outer;
        } else {
            float outer = range + 0.5f;
            outerSq = outer * outer;
            if (targetUnit->IsLargeUnit()) predict = true;   // the planner's "moving"
        }
        if ((kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn) && !order->pointIsUseSpot &&
            targetUnit->IsLargeUnit()) {
            // A moving target of a missile spell: aim where it will be when the spell arrives.
            predict = true;
            Spell* missile = SpellOfSource(order->source);
            if (missile && (missile->targetFlags & SpellTargetFlag::AtPoint)) {
                float flight = missile->speed;
                if (Greater(flight, 0.0f)) castDelay = CastDelayTicks(distanceSq, flight);
            }
        }
    }

    plan->rangeSq = outerSq;
    float reach = reachSq;
    float best = kBestStart;
    for (int tick = 0; tick < ticks; ++tick) {
        if (predict) {
            aim = order->target->PositionAt(tick + castDelay + duration + now);
            delta = aim - origin;
        }
        float squared = LengthSquared2D(XY(delta));
        bool tryHere = true;
        if (Greater(squared, reach)) {
            // Out of reach: wait while the target comes closer, give up once it moves off.
            if (!Greater(squared, best)) {
                tryHere = false;
            } else if (squared >= outerSq) {
                break;
            }
        }
        if (tryHere) {
            bool facingIt = XY(delta) == Vector2{0.0f, 0.0f} || (targetUnit && targetUnit->id == unit->id);
            int turnTicks = -1;
            if (!facingIt) {
                float turn = lib::Atan2(delta.y, delta.x) - heading;
                turn = AbsBits(turn);
                float angle = turn;
                if (Greater(turn, kPi)) angle = kPi + kPi - turn;
                if (!Greater(angle, kPercent)) {
                    facingIt = true;
                } else {
                    turnTicks = lib::Truncate(angle / turnSpeed);
                    if (turnTicks + 1 <= tick) facingIt = true;
                }
            }
            if (facingIt) {
                if (!CanActFrom(order, aim, tick + duration + now)) break;
                plan->targetId = order->target ? order->target->id : -1;
                plan->startTime = tick + now;
                plan->endTime = tick + now + duration;
                plan->animation = steps;
                plan->calledShot = order->source != kAttackAllParts;
                plan->direction = delta;
                if (order->kind == UnitOrderKind::CastAt || order->kind == UnitOrderKind::CastOn) FillCastKind(plan, spell);
                return true;
            }
        }
        best = squared;
    }
    return false;
}

bool UnitServer::CanPlanAction(const AIOrder* order) {
    OrderPlan plan{};
    MapObject* target = order->target;
    if (IsUnit(target)) target->flags |= MapObjectFlag::Planned;
    bool result = PlanAction(order, &plan);
    if (IsUnit(target)) target->flags &= ~MapObjectFlag::Planned;
    return result;
}

// --- The spell line-of-sight test ----------------------------------------------------------

// The spell line-of-sight test the world's spell casting asks per target.

bool SpellHasLineOfSight(WorldServer* world, Spell* spell, Unit* caster, const Vector3& from,
                                    Unit* target, const Vector3& at) {
    const auto* record = spell->GetPrototype();
    if (!record->needsSight) return true;
    Vector3 to;
    if (!target) {
        to = at;
        to.z = world->SurfaceHeightAt(to.x, to.y) + kAboveSurface;
    } else {
        to = {target->localPosition.x, target->localPosition.y, target->localPosition.z};
        float ground = target->GroundHeight(to.x, to.y);
        to.z = target->ModeSpeed() + ground;
    }
    Vector3 eye = from;
    if (caster) {
        float ground = caster->GroundHeight(eye.x, eye.y);
        eye.z = caster->BaseSpeed() + ground;
    }
    // Below the minimum, or NaN, fails.
    return world->aiMap.LineOfSight(eye, to) >= kMinSight;
}
