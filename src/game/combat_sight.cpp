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

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ai.h"
#include "originals_units.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

// Values the original keeps on the FPU stack are double locals, values it stores (FSTP to memory)
// are float locals.

namespace {
constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kHalf = 0.5f;                                       // 0073B86C
constexpr float kOne = 1.0f;                                        // 0073EF24 / 0073EEE4
constexpr float kPointOne = std::bit_cast<float>(0x3DCCCCCDu);      // 0073B850 / 0073EEC0 (0.1)
constexpr float kPointThree = std::bit_cast<float>(0x3E99999Au);    // 0073C54C (0.3)
constexpr float kMinusPointThree = std::bit_cast<float>(0xBE99999Au); // 0073CDFC (-0.3)
constexpr float kPointNine = std::bit_cast<float>(0x3F666666u);     // 0073CD70 (0.9)
constexpr float kRangeSlack = std::bit_cast<float>(0x3F866666u);    // 0073CEE0 (1.05)
constexpr float kMeleeReach = 3.0f;                                 // 0073BAA4
constexpr float kFifteen = 15.0f;                                   // 0073C518
constexpr float kPi = std::bit_cast<float>(0x40490FDBu);            // 0073EEBC
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0073C548 (0.01)
constexpr float kNearSq = 10.0f;                                    // immediate 0x41200000
constexpr double kFrontCosine = 0.5;                                // 0073CC88
constexpr std::uint32_t kNoRangeBits = 0x501502F9u;                 // 1e10: point orders with flag18
constexpr std::uint32_t kFarBits = 0x7E967699u;                     // 1e38: the plan's initial range
constexpr std::uint32_t kBestStartBits = 0x7149F2CAu;               // 1e30

constexpr int kOrderAttack = 3;
constexpr int kOrderCast = 4;
constexpr int kOrderCastAt = 5;
constexpr int kOrderSkill = 6;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kSkillSource = 6;               // plan +0x30 is set for other sources
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kFlagNoCasting = 0x10000000;     // CUnit flags_
constexpr std::uint32_t kFlagPlanned = 0x8000000;        // set on the target while planning
constexpr std::uint32_t kSpellNoTarget = 0x20000000;     // spell +0x3C: aimed at no object
constexpr std::uint32_t kSpellKindStrike = 0x28;         // spell +0x18 >> 16
constexpr std::uint32_t kSpellKindCurse = 0x29;
constexpr int kCastKindNone = 8;

template <typename T>
T Get(const void* object, int offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(T));
    return value;
}

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// x87 compares as the original's TEST AH masks read them.
bool Greater(double a, double b) { return a > b; }   // TEST AH,0x41 not taken

bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }

CItemWeapon* ActiveWeapon(CUnit* unit) { return static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon)); }

// _ftol (006E853C) of a / b, the quotient as the FPU leaves it (at the game's 24- or 53-bit
// precision a double holds it exactly).
int FtolQuotient(double a, double b) { return static_cast<int>(gamecrt::Ftol(a / b)); }

// The missile spell's delay in ticks: _ftol(sqrt(distanceSq) * 15.0 / flight - 3.0), on the FPU
// stack throughout (the double constants at 0073F0A8 / 0073F0A0).
int CastDelayTicks(double distanceSq, double flight) {
    return static_cast<int>(gamecrt::Ftol(std::sqrt(distanceSq) * 15.0 / flight - 3.0));
}

// The spell a cast order's source names: a quick item's spell (+0x44), or the unit's own. The
// boolean is false when a quick item source has no carrier (the original returns there).
bool OrderSpell(CUnitServer* self, std::uint32_t source, void** spell) {
    if (source & kQuickItemSource) {
        CItem* carrier = self->SpellCarrier(source);
        if (!carrier) return false;
        *spell = Get<void*>(carrier, 0x44);
        return true;
    }
    *spell = self->unit->GetSpellObject(source);
    return true;
}

char* SpellPrototypeOf(void* spell) { return static_cast<CSpell*>(spell)->GetPrototype(); }
} // namespace

namespace {
// Not rewritten (forwarders), as __fastcall with the unused EDX for __thiscall:
// 00530280. CUnit: the height of the ground (or water surface) under (x, y) for the unit, in ST0.
// 00557B80. CArray::SetSize of an AnimationSteps list.
// 00455950. The world an object lives in.

} // namespace

// --- What must be seen -----------------------------------------------------------------------

bool CUnitServer::FindSightTarget(const UnitAction* order, SightTarget* out) {
    if (order->flag18) return false;
    void* world = ManagerContext();
    CEngineObject* target = order->target;

    if (order->kind == kOrderCast || order->kind == kOrderCastAt) {
        void* spell;
        if (!OrderSpell(this, order->source, &spell) || !spell) return false;
        if (Get<int>(SpellPrototypeOf(spell), 0x40) == 0) return false;
        out->point.x = out->point.y = out->point.z = 0.0f;
        std::memset(&out->minDistanceSq, 0, sizeof(float));
        out->unit = nullptr;
        if (IsUnit(target)) {
            CUnit* other = static_cast<CUnit*>(target);
            out->unit = other;
            double reach = static_cast<double>(unit->standingHeight) + other->standingHeight + kPointThree;
            out->minDistanceSq = static_cast<float>(reach * reach);
            out->eyeHeight = static_cast<float>(unit->BaseSpeed());
            return true;
        }
        std::memcpy(&out->point, &order->point, sizeof(out->point));
        out->point.z = static_cast<float>(
            static_cast<CWorld*>(world)->SurfaceHeightAt(out->point.x, out->point.y) + kOne);
        out->eyeHeight = static_cast<float>(unit->BaseSpeed());
        return true;
    }

    if (!IsUnit(target)) return false;
    out->point.x = out->point.y = out->point.z = 0.0f;
    out->minDistanceSq = kNearSq;
    out->unit = static_cast<CUnit*>(target);
    out->eyeHeight = static_cast<float>(unit->BaseSpeed());
    return true;
}
EI_REPLACE_METHOD(0x0054BCB0, CUnitServer::FindSightTarget);

// --- Can the order be carried out from here ----------------------------------------------------

namespace {
// AI units only act on a target within 1.05 times their sight range: the smaller of stats +0xC4
// and the movement factor +0x904 times stats +0xB4 (the original selects the dwords with a mask
// built from FCOMP's C0, so a NaN product picks the first).
bool OutOfSightRange(CUnitServer* self, const float* point) {
    float limit = Get<float>(self, 0x2A4);
    float range = Get<float>(self, 0x904) * Get<float>(self, 0x294);
    float rangeValue = range;
    std::uint32_t chosenBits = rangeValue >= limit ? Bits(rangeValue) : Bits(limit);
    float chosen = std::bit_cast<float>(chosenBits);
    double reach = static_cast<double>(chosen) * kRangeSlack;
    CUnit* unit = self->unit;
    double dy = static_cast<double>(unit->position.y) - point[1];
    double dx = static_cast<double>(unit->position.x) - point[0];
    EI_NAN_AUDIT(reach, dx, dy);
    return reach * reach < LengthSquared2DUnrounded(dx, dy);
}

// A melee or skill order fails when a living unit other than the target stands between the unit
// and `point` at `time`: within 0.1..0.9 along the way (in units of the squared distance, sic)
// and less than 0.3 to the side.
bool SomeoneInTheWay(CUnitServer* self, const UnitAction* order, const float* point, int time) {
    CUnit* unit = self->unit;
    float dx = point[0] - unit->position.x;
    float dy = point[1] - unit->position.y;
    float dz = point[2] - unit->position.z;
    double lengthSq = LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
    float ux = static_cast<float>(dx / lengthSq);
    float uy = static_cast<float>(dy / lengthSq);
    float uz = static_cast<float>(dz / lengthSq);
    double negX = -static_cast<double>(ux);
    float side = static_cast<float>(std::sqrt(negX * negX + static_cast<double>(uy) * uy));
    float vx = static_cast<float>(uy / side);
    float vy = static_cast<float>(negX / side);
    float vz = static_cast<float>(kZero / side);

    // The units the brain lists as nearby (AIBrain +0xF4).
    for (auto* node = self->Brain()->nearby.head; node;) {
        CEngineObject* other = node->data;
        node = node->next;
        if (other == order->target) continue;
        if (ObjectSlots(other)->IsDead() != 0) continue;
        Vector3 scratch;
        const Vector3* p = ObjectSlots(other)->PositionAt(&scratch, time);
        double a = static_cast<double>(p->x) - unit->position.x;
        double b = static_cast<double>(p->y) - unit->position.y;
        double c = static_cast<double>(p->z) - unit->position.z;
        float along = static_cast<float>(c * uz + b * uy + a * ux);
        float across = static_cast<float>(c * vz + b * vy + a * vx);
        if (Greater(along, kPointOne) && along < kPointNine && Greater(across, kMinusPointThree) &&
            across < kPointThree)
            return true;
    }
    return false;
}

// Does the order need the way to its target clear of units (melee or skill)?
bool NeedsClearWay(CUnitServer* self, const UnitAction* order) {
    if (order->kind == kOrderSkill) return true;
    if (order->kind != kOrderAttack) return false;
    CItemWeapon* weapon = ActiveWeapon(self->unit);
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) return false;
    weapon = ActiveWeapon(self->unit);
    float reach = weapon ? weapon->reach_ : self->Combat().reach;
    EI_NAN_AUDIT(reach);
    return reach < kMeleeReach;
}
} // namespace

bool CUnitServer::CanActFrom(const UnitAction* order, const float* point, int time) {
    if (order->target == unit) return true;
    if (order->flag18) return true;
    if (!unit->player && order->target && OutOfSightRange(this, point)) return false;

    if (order->kind == kOrderCast || order->kind == kOrderCastAt) {
        if (unit->flags_ & kFlagNoCasting) return false;
    } else if (NeedsClearWay(this, order) && SomeoneInTheWay(this, order, point, time)) {
        return false;
    }

    SightTarget sight;
    if (!FindSightTarget(order, &sight)) return true;
    if (sight.unit) {
        // A unit target is seen at `point`, at its own height above the ground there.
        std::memcpy(&sight.point, point, sizeof(sight.point));
        float ground = static_cast<float>(static_cast<CUnit*>((void*)sight.unit)->GroundHeight(point[0], point[1]));
        sight.point.z = static_cast<float>(sight.unit->ModeSpeed() + ground);
        sight.unit = nullptr;
        // (The original then asks a remaining sight.unit for slot 48; it is always null here.)
    }
    double apartSq = DistanceSquaredXYUnrounded<double>(sight.point, unit->position);
    EI_NAN_AUDIT(apartSq, sight.minDistanceSq);
    if (apartSq < sight.minDistanceSq) return true;

    Vector3 eye;
    eye.x = unit->position.x;
    eye.y = unit->position.y;
    eye.z = unit->position.z;
    eye.z = static_cast<float>(static_cast<CUnit*>((void*)unit)->GroundHeight(eye.x, eye.y) + sight.eyeHeight);
    CAIMap* map = static_cast<CWorldServer*>(ManagerContext())->AIMap();
    double visibility = map->LineOfSight(&eye, &sight.point);
    return visibility >= kPointOne;
}
EI_REPLACE_METHOD(0x0054C2F0, CUnitServer::CanActFrom);

// --- The planner -----------------------------------------------------------------------------

namespace {
void InitSteps(AnimationSteps* steps) {
    std::memset(steps, 0, sizeof(*steps));
    steps->vtable = kAnimationStepsVtable;
}

void DestroySteps(AnimationSteps* steps) {
    steps->vtable = kAnimationStepsBaseVtable;
    if (steps->steps) gamecrt::operator_delete(steps->steps);
}

// The quick item or spell value the cast animation is built with: the spell's +0x4C scaled by
// 15 / stats +0x30 (left at stats +0x30 when that is 0), rounded.
int CastValue(CUnitServer* self, void* spell) {
    double scale = Get<float>(self, 0x210);
    if (scale != 0.0) scale = kFifteen / scale;   // a NaN stays the same NaN
    volatile std::int64_t wide = Get<std::uint32_t>(spell, 0x4C);
    float value = static_cast<float>(static_cast<double>(wide) * scale);
    return x87::fistp(value);
}

void FillCastKind(OrderPlan* plan, void* spell) {
    plan->isCast = 1;
    if (!spell) {
        plan->castKind = kCastKindNone;
        return;
    }
    if (Get<std::uint32_t>(spell, 0x3C) & kSpellNoTarget) plan->targetId = -1;
    std::uint32_t kind = Get<std::uint32_t>(spell, 0x18) >> 16;
    if (kind == kSpellKindStrike) plan->castKind = 1;
    else if (kind == kSpellKindCurse) plan->castKind = 0;
    else plan->castKind = Get<int>(SpellPrototypeOf(spell), 0x14);
}
} // namespace

bool CUnitServer::PlanAction(const UnitAction* order, OrderPlan* plan) {
    AnimationSteps steps;
    InitSteps(&steps);
    Vector3 origin;
    std::memcpy(&origin, &unit->position.x, sizeof(origin));
    int now = static_cast<CWorld*>(ObjectWorld(this))->tick_;
    void* spell = nullptr;

    if (order->flag18) {
        double distance = ActionDistance(order);
        double limitSq = distance * distance;
        double apartSq = DistanceSquaredXYUnrounded<double>(order->point, unit->position);
        EI_NAN_AUDIT(limitSq, apartSq);
        if (limitSq < apartSq) {
            DestroySteps(&steps);
            return false;
        }
    }

    plan->animation.SetSize(0, -1);
    plan->hitLocation = 1;
    plan->animation.extra = -1;
    plan->targetId = -1;
    plan->rangeSq = std::bit_cast<float>(kFarBits);
    plan->endTime = 0;
    plan->startTime = 0;
    plan->isCast = 0;
    plan->notSkill = 0;
    plan->castKind = kCastKindNone;

    // The animation and its duration (the builders always set it; the original's is not
    // initialised).
    int duration = 0;
    int kind = order->kind;
    if (kind == kOrderAttack) {
        BuildAttackSteps(plan, &steps, &duration, order->target);
    } else if (kind < kOrderCast || kind > kOrderCastAt) {
        BuildOrderSteps(&steps, &duration, kind, order->source);
    } else {
        spell = SpellOfSource(order->source);
        int value = 0;
        if (spell) value = CastValue(this, spell);
        BuildCastSteps(&steps, &duration, order->kind, value);
    }
    if (duration <= 0) duration = 1;

    float range = static_cast<float>(ActionDistance(order));
    if (order->flag18) range = std::bit_cast<float>(kNoRangeBits);

    // The unit's heading and how many ticks a half turn takes.
    Vector3 axis;
    unit->orientation.AxisY(&axis);
    Vector3 facing = {-axis.x, -axis.y, -axis.z};
    float heading = static_cast<float>(x87::fpatan(facing.y, facing.x));
    float forward, turnSpeed;
    unit->GetMoveSpeeds(&forward, &turnSpeed);
    float halfTurn = kPi / turnSpeed;
    int ticks = x87::fistp(halfTurn) + 3;

    bool predict = false;
    CUnit* targetUnit = nullptr;
    int castDelay = 0;

    Vector3 aim;
    if (order->target) ObjectSlots(order->target)->AimPoint(&aim);
    else std::memcpy(&aim, &order->point, sizeof(aim));
    Vector3 delta;
    delta.x = aim.x - origin.x;
    delta.y = aim.y - origin.y;
    delta.z = aim.z - origin.z;
    double ox = static_cast<double>(origin.x) - aim.x;
    double oy = static_cast<double>(origin.y) - aim.y;
    float distanceSq = static_cast<float>(LengthSquared2DUnrounded(oy, ox));

    float reachSq;    // the order's own range, squared
    float outerSq;    // how far the target may be and still be waited for
    if (!IsUnit(order->target)) {
        reachSq = static_cast<float>(static_cast<double>(range) * range);
        outerSq = reachSq;
    } else {
        targetUnit = static_cast<CUnit*>(order->target);
        double speed = targetUnit->MoveSpeed();
        reachSq = static_cast<float>(static_cast<double>(range) * range);
        bool inFront = false;
        if (!Greater(distanceSq, reachSq)) {
            double dot = DotUnrounded<Sum3::ZYX, double>(delta.x, delta.y, delta.z, facing.x, facing.y, facing.z);
            double cone = std::sqrt(distanceSq) * kFrontCosine;
            EI_NAN_AUDIT(cone, dot);
            inFront = cone < dot;
        }
        if (inFront) {
            // Within reach and in front: the target may walk off for half the turn and the action.
            int wait = ticks / 2 + duration;
            double outer = speed * (static_cast<double>(wait) * (static_cast<double>(kOne) * kHalf));
            outer = outer + range;
            outer = outer + kHalf;
            outerSq = static_cast<float>(outer * outer);
        } else {
            double outer = static_cast<double>(range) + kHalf;
            outerSq = static_cast<float>(outer * outer);
            if (targetUnit->IsLargeUnit()) predict = true;   // slot 54 (the planner's "moving")
        }
        if ((kind == kOrderCast || kind == kOrderCastAt) && !order->flag18 &&
            targetUnit->IsLargeUnit()) {
            // A moving target of a missile spell: aim where it will be when the spell arrives.
            predict = true;
            void* missile;
            if (OrderSpell(this, order->source, &missile) && missile &&
                (Get<std::uint32_t>(missile, 0x3C) & kSpellNoTarget)) {
                float flight = Get<float>(missile, 0x30);
                if (Greater(flight, kZero)) castDelay = CastDelayTicks(distanceSq, flight);
            }
        }
    }

    plan->rangeSq = outerSq;
    float reach = reachSq;
    float best = std::bit_cast<float>(kBestStartBits);
    for (int tick = 0; tick < ticks; ++tick) {
        if (predict) {
            Vector3 scratch;
            const Vector3* p = ObjectSlots(order->target)->PositionAt(&scratch, tick + castDelay + duration + now);
            std::memcpy(&aim, p, sizeof(aim));
            delta.x = aim.x - origin.x;
            delta.y = aim.y - origin.y;
            delta.z = aim.z - origin.z;
        }
        double squared = LengthSquared2DUnrounded<double>(delta.y, delta.x);
        float stored = static_cast<float>(squared);
        bool tryHere = true;
        if (Greater(squared, reach)) {
            // Out of reach: wait while the target comes closer, give up once it moves off.
            if (!Greater(stored, best)) {
                tryHere = false;
            } else if (stored >= outerSq) {
                break;
            }
        }
        if (tryHere) {
            EI_NAN_AUDIT(delta.x, delta.y);
            bool facingIt = (delta.x == 0.0f && delta.y == 0.0f) ||
                            (targetUnit && targetUnit->id_ == unit->id_);
            int turnTicks = -1;
            if (!facingIt) {
                float turn = static_cast<float>(x87::fpatan(delta.y, delta.x) - heading);
                turn = std::bit_cast<float>(Bits(turn) & 0x7FFFFFFFu);
                double angle = turn;
                if (Greater(turn, kPi)) angle = static_cast<double>(kPi) + kPi - turn;
                if (!Greater(angle, kPercent)) {
                    facingIt = true;
                } else {
                    turnTicks = FtolQuotient(angle, turnSpeed);
                    if (turnTicks + 1 <= tick) facingIt = true;
                }
            }
            if (facingIt) {
                if (!CanActFrom(order, &aim.x, tick + duration + now)) break;
                plan->targetId = order->target ? order->target->id_ : -1;
                plan->startTime = tick + now;
                plan->endTime = tick + now + duration;
                plan->animation.SetSize(steps.count, -1);
                for (int i = 0; i < steps.count; ++i) plan->animation.steps[i] = steps.steps[i];
                plan->animation.extra = steps.extra;
                plan->notSkill = order->source != kSkillSource;
                plan->direction.x = delta.x;
                plan->direction.y = delta.y;
                plan->direction.z = delta.z;
                if (order->kind == kOrderCast || order->kind == kOrderCastAt) FillCastKind(plan, spell);
                DestroySteps(&steps);
                return true;
            }
        }
        best = stored;
    }
    DestroySteps(&steps);
    return false;
}
EI_REPLACE_METHOD(0x0054CA40, CUnitServer::PlanAction);

bool CUnitServer::CanPlanAction(const UnitAction* order) {
    OrderPlan plan;
    std::memset(&plan, 0, sizeof(plan));
    plan.animation.vtable = kAnimationStepsVtable;
    CEngineObject* target = order->target;
    if (IsUnit(target)) target->flags_ |= kFlagPlanned;
    bool result = PlanAction(order, &plan);
    if (IsUnit(target)) target->flags_ &= ~kFlagPlanned;
    DestroySteps(&plan.animation);
    return result;
}
EI_REPLACE_METHOD(0x0054C6B0, CUnitServer::CanPlanAction);
