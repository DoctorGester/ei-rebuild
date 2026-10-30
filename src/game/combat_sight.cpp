// Combat, third part: whether an order can be carried out from where the unit stands (reach, units
// in the way, line of sight) and the planner that finds the moment it can. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Order lifecycle").
#include "ai_map.h"
#include "combat.h"
#include "combat_plan.h"

#include "item.h"
#include "maths.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "lib/float_select.h"

#include <bit>
#include <cstring>

// The original's float maths ran at the main thread's 24-bit precision: float maths in its order here, and an int
// or a double operand through lib/pc24.h's one rounding.

namespace {
constexpr float kPointOne = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kPointOne) == 0x3DCCCCCDu);
constexpr float kPointThree = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kPointThree) == 0x3E99999Au);
constexpr float kMinusPointThree = -0.3f;
static_assert(std::bit_cast<std::uint32_t>(kMinusPointThree) == 0xBE99999Au);
constexpr float kPointNine = 0.9f;
static_assert(std::bit_cast<std::uint32_t>(kPointNine) == 0x3F666666u);
constexpr float kRangeSlack = 1.05f;
static_assert(std::bit_cast<std::uint32_t>(kRangeSlack) == 0x3F866666u);
constexpr float kMeleeReach = 3.0f;
constexpr float kFifteen = 15.0f;
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kPercent) == 0x3C23D70Au);
constexpr float kNearSq = 10.0f;
constexpr float kFrontCosine = 0.5f;
constexpr std::uint32_t kNoRangeBits = 0x501502F9u;                 // 1e10: point orders with pointIsUseSpot
constexpr std::uint32_t kFarBits = 0x7E967699u;                     // 1e38: the plan's initial range
constexpr std::uint32_t kBestStartBits = 0x7149F2CAu;               // 1e30

constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kSkillSource = 6;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr int kCastKindNone = 8;

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// An x87 compare as the original reads it (false for a NaN).
bool Greater(float a, float b) { return a > b; }

bool IsUnit(EngineObject* object) { return object && object->GetClassId() == ClassId::Unit; }

// _ftol: truncated through 64 bits, the low half (0 for a NaN).
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }

// The missile spell's delay in ticks: _ftol(sqrt(distanceSq) * 15 / flight - 3).
int CastDelayTicks(float distanceSq, float flight) {
    return Truncate(std::sqrt(distanceSq) * 15.0f / flight - 3.0f);
}

// The spell a cast order's source names: a quick item's spell, or the unit's own. The boolean is false when a
// quick item source has no carrier (the original returns there).
bool OrderSpell(UnitServer* self, std::uint32_t source, CSpell** spell) {
    if (source & kQuickItemSource) {
        Item* carrier = self->SpellCarrier(source);
        if (!carrier) return false;
        *spell = carrier->spell;
        return true;
    }
    *spell = self->unit->GetSpellObject(source);
    return true;
}

// AI units only act on a target within 1.05 times their sight range: the smaller of two stats values (the
// movement factor times the sight); the original's select picks the first for a NaN product.
bool OutOfSightRange(UnitServer* self, const float* point) {
    float limit = self->stats.perception[2].current;
    float range = self->moveFactors[3] * self->stats.perception[0].current;
    float rangeValue = range;
    std::uint32_t chosenBits = rangeValue >= limit ? Bits(rangeValue) : Bits(limit);
    float chosen = std::bit_cast<float>(chosenBits);
    float reach = chosen * kRangeSlack;
    CUnit* unit = self->unit;
    float dy = unit->localPosition.y - point[1];
    float dx = unit->localPosition.x - point[0];
    EI_NAN_AUDIT(reach, dx, dy);
    return reach * reach < LengthSquared2DUnrounded(dx, dy);
}

// A melee or skill order fails when a living unit other than the target stands between the unit
// and `point` at `time`: within 0.1..0.9 along the way (in units of the squared distance, sic)
// and less than 0.3 to the side.
bool SomeoneInTheWay(UnitServer* self, const AIOrder* order, const float* point, int time) {
    CUnit* unit = self->unit;
    float dx = point[0] - unit->localPosition.x;
    float dy = point[1] - unit->localPosition.y;
    float dz = point[2] - unit->localPosition.z;
    float lengthSq = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
    float ux = dx / lengthSq;
    float uy = dy / lengthSq;
    float uz = dz / lengthSq;
    float negX = -ux;
    float side = std::sqrt(negX * negX + uy * uy);
    float vx = uy / side;
    float vy = negX / side;
    float vz = 0.0f / side;

    // The units the brain lists as nearby.
    for (auto node = self->brain.memory.nearby.begin(); node != self->brain.memory.nearby.end();) {
        CUnit* other = *node;
        ++node;
        if (other == order->target) continue;
        if (other->IsDead() != 0) continue;
        Vector3 scratch;
        const Vector3* p = other->PositionAt(&scratch, time);
        float a = p->x - unit->localPosition.x;
        float b = p->y - unit->localPosition.y;
        float c = p->z - unit->localPosition.z;
        float along = c * uz + b * uy + a * ux;
        float across = c * vz + b * vy + a * vx;
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
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return false;
    weapon = self->unit->ActiveWeapon();
    float reach = weapon ? weapon->reach : self->stats.combat[UnitStats::kReach];
    EI_NAN_AUDIT(reach);
    return reach < kMeleeReach;
}

// The quick item or spell value the cast animation is built with: the spell's castTime scaled by
// 15 / stats.rate (unscaled when that is 0), rounded.
int CastValue(UnitServer* self, CSpell* spell) {
    float scale = self->stats.rate;
    if (scale != 0.0f) scale = kFifteen / scale;   // a NaN stays the same NaN
    float value = pc24::Mul(static_cast<double>(spell->castTime), scale);   // the dword exact, one rounding
    return lib::RoundEven(value);
}

void FillCastKind(OrderPlan* plan, CSpell* spell) {
    plan->isCast = 1;
    if (!spell) {
        plan->castKind = kCastKindNone;
        return;
    }
    if (spell->targetFlags & SpellTargetFlag::AtPoint) plan->targetId = -1;
    SpellType type = spell->Type();
    if (type == SpellType::RickMagic) plan->castKind = 1;
    else if (type == SpellType::CurseMagic) plan->castKind = 0;
    else plan->castKind = spell->GetPrototype()->school;
}

} // namespace

// --- What must be seen -----------------------------------------------------------------------

bool UnitServer::FindSightTarget(const AIOrder* order, SightTarget* out) {
    if (order->pointIsUseSpot) return false;
    WorldServer* world = ServerWorld();
    CMapObject* target = order->target;

    if (order->kind == UnitOrderKind::CastAt || order->kind == UnitOrderKind::CastOn) {
        CSpell* spell;
        if (!OrderSpell(this, order->source, &spell) || !spell) return false;
        if (spell->GetPrototype()->needsSight == 0) return false;
        out->point.x = out->point.y = out->point.z = 0.0f;
        out->minDistanceSq = 0.0f;
        out->unit = nullptr;
        if (CUnit* other = UnitOrNull(target)) {
            out->unit = other;
            float reach = unit->standingHeight + other->standingHeight + kPointThree;
            out->minDistanceSq = reach * reach;
            out->eyeHeight = unit->BaseSpeed();
            return true;
        }
        out->point = order->point;
        out->point.z = pc24::Add(world->SurfaceHeightAt(out->point.x, out->point.y), 1.0f);   // a double plus a float
        out->eyeHeight = unit->BaseSpeed();
        return true;
    }

    CUnit* targetUnit = UnitOrNull(target);
    if (!targetUnit) return false;
    out->point.x = out->point.y = out->point.z = 0.0f;
    out->minDistanceSq = kNearSq;
    out->unit = targetUnit;
    out->eyeHeight = unit->BaseSpeed();
    return true;
}

// --- Can the order be carried out from here ----------------------------------------------------

bool UnitServer::CanActFrom(const AIOrder* order, const float* point, int time) {
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
        lib::LoadBytes(sight.point, point);
        float ground = static_cast<float>(sight.unit->GroundHeight(point[0], point[1]));
        sight.point.z = sight.unit->ModeSpeed() + ground;
        sight.unit = nullptr;
        // (The original then asks a remaining sight.unit for a virtual; it is always null here.)
    }
    float apartSq = DistanceSquaredXYUnrounded<float>(sight.point, unit->localPosition);
    EI_NAN_AUDIT(apartSq, sight.minDistanceSq);
    if (apartSq < sight.minDistanceSq) return true;

    Vector3 eye;
    eye.x = unit->localPosition.x;
    eye.y = unit->localPosition.y;
    eye.z = unit->localPosition.z;
    eye.z = pc24::Add(unit->GroundHeight(eye.x, eye.y), sight.eyeHeight);   // the double ground height plus a float
    AIMap* map = &ServerWorld()->aiMap;
    float visibility = map->LineOfSight(eye, sight.point);
    return visibility >= kPointOne;
}

// --- The planner -----------------------------------------------------------------------------

bool UnitServer::PlanAction(const AIOrder* order, OrderPlan* plan) {
    AnimationSteps steps;
    Vector3 origin;
    std::memcpy(&origin, &unit->localPosition.x, sizeof(origin));
    int now = World()->tick;
    CSpell* spell = nullptr;

    if (order->pointIsUseSpot) {
        float distance = ActionDistance(order);
        float limitSq = distance * distance;
        float apartSq = DistanceSquaredXYUnrounded<float>(order->point, unit->localPosition);
        EI_NAN_AUDIT(limitSq, apartSq);
        if (limitSq < apartSq) return false;
    }

    plan->animation = {};
    plan->hitLocation = 1;
    plan->targetId = -1;
    plan->rangeSq = std::bit_cast<float>(kFarBits);
    plan->endTime = 0;
    plan->startTime = 0;
    plan->isCast = 0;
    plan->notSkill = 0;
    plan->castKind = kCastKindNone;

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
    if (order->pointIsUseSpot) range = std::bit_cast<float>(kNoRangeBits);

    // The unit's heading and how many ticks a half turn takes.
    Vector3 axis;
    unit->localRotation.AxisY(&axis);
    Vector3 facing = {-axis.x, -axis.y, -axis.z};
    float heading = pc24::Round(pc24::Atan2(facing.y, facing.x));
    float forward, turnSpeed;
    unit->GetMoveSpeeds(&forward, &turnSpeed);
    float halfTurn = kPi / turnSpeed;
    int ticks = lib::RoundEven(halfTurn) + 3;

    bool predict = false;
    CUnit* targetUnit = nullptr;
    int castDelay = 0;

    Vector3 aim;
    if (order->target) order->target->AimPoint(&aim);
    else aim = order->point;
    Vector3 delta;
    delta.x = aim.x - origin.x;
    delta.y = aim.y - origin.y;
    delta.z = aim.z - origin.z;
    float ox = origin.x - aim.x;
    float oy = origin.y - aim.y;
    float distanceSq = LengthSquared2DUnrounded(oy, ox);

    float reachSq;    // the order's own range, squared
    float outerSq;    // how far the target may be and still be waited for
    targetUnit = UnitOrNull(order->target);
    if (!targetUnit) {
        reachSq = range * range;
        outerSq = reachSq;
    } else {
        float speed = static_cast<float>(targetUnit->MoveSpeed());   // CUnit::MoveSpeed: a float
        reachSq = range * range;
        bool inFront = false;
        if (!Greater(distanceSq, reachSq)) {
            float dot = DotUnrounded<Sum3::ZYX>(delta.x, delta.y, delta.z, facing.x, facing.y, facing.z);
            float cone = std::sqrt(distanceSq) * kFrontCosine;
            EI_NAN_AUDIT(cone, dot);
            inFront = cone < dot;
        }
        if (inFront) {
            // Within reach and in front: the target may walk off for half the turn and the action.
            int wait = ticks / 2 + duration;
            // The int halved rounds once: as the int rounded to a float, halved.
            float outer = speed * (static_cast<float>(wait) * 0.5f);
            outer = outer + range;
            outer = outer + 0.5f;
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
            CSpell* missile;
            if (OrderSpell(this, order->source, &missile) && missile &&
                (missile->targetFlags & SpellTargetFlag::AtPoint)) {
                float flight = missile->speed;
                if (Greater(flight, 0.0f)) castDelay = CastDelayTicks(distanceSq, flight);
            }
        }
    }

    plan->rangeSq = outerSq;
    float reach = reachSq;
    float best = std::bit_cast<float>(kBestStartBits);
    for (int tick = 0; tick < ticks; ++tick) {
        if (predict) {
            Vector3 scratch;
            const Vector3* p = order->target->PositionAt(&scratch, tick + castDelay + duration + now);
            aim = *p;
            delta.x = aim.x - origin.x;
            delta.y = aim.y - origin.y;
            delta.z = aim.z - origin.z;
        }
        float squared = LengthSquared2DUnrounded(delta.y, delta.x);
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
            EI_NAN_AUDIT(delta.x, delta.y);
            bool facingIt = (delta.x == 0.0f && delta.y == 0.0f) ||
                            (targetUnit && targetUnit->id == unit->id);
            int turnTicks = -1;
            if (!facingIt) {
                float turn = pc24::Sub(pc24::Atan2(delta.y, delta.x), heading);
                turn = AbsBits(turn);
                float angle = turn;
                if (Greater(turn, kPi)) angle = kPi + kPi - turn;
                if (!Greater(angle, kPercent)) {
                    facingIt = true;
                } else {
                    turnTicks = Truncate(angle / turnSpeed);
                    if (turnTicks + 1 <= tick) facingIt = true;
                }
            }
            if (facingIt) {
                if (!CanActFrom(order, &aim.x, tick + duration + now)) break;
                plan->targetId = order->target ? order->target->id : -1;
                plan->startTime = tick + now;
                plan->endTime = tick + now + duration;
                plan->animation = steps;
                plan->notSkill = order->source != kSkillSource;
                plan->direction.x = delta.x;
                plan->direction.y = delta.y;
                plan->direction.z = delta.z;
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
    CMapObject* target = order->target;
    if (IsUnit(target)) target->flags |= MapObjectFlag::Planned;
    bool result = PlanAction(order, &plan);
    if (IsUnit(target)) target->flags &= ~MapObjectFlag::Planned;
    return result;
}
