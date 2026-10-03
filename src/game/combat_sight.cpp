// Combat, third part: whether an order can be carried out from where the unit stands (reach, units
// in the way, line of sight) and the planner that finds the moment it can. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Third round").
#include "combat.h"
#include "combat_plan.h"

#include "item.h"
#include "maths.h"
#include "spell.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>

// Keep the original's order of operations: values it keeps on the FPU stack are double locals,
// values it stores (FSTP to memory) are float (volatile where the compiler might keep them).
#pragma float_control(precise, on)

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
constexpr double kCastDelayScale = 15.0;                            // 0073F0A8
constexpr double kCastDelayBias = 3.0;                              // 0073F0A0
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

// Virtual slots of map objects / units.
constexpr int kSlotPositionAt = 0xD0 / 4;   // Vector3* (Vector3* out, int time): where it will be
constexpr int kSlotAimPoint = 0xD4 / 4;     // Vector3* (Vector3* out)
constexpr int kSlotIsMoving = 0xD8 / 4;     // int ()
constexpr int kSlotIsDead = 0xDC / 4;       // int ()
constexpr int kSlotSpeed = 0xEC / 4;        // ST0 ()
constexpr int kWorldSurfaceHeight = 0x78 / 4;   // ST0 (x, y)

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}
template <typename T>
T Get(const void* object, int offset) {
    T value;
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(T));
    return value;
}

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// x87 compares as the original's TEST AH masks read them.
bool LessOrUnordered(double a, double b) { return !(a >= b); }   // TEST AH,1 taken
bool Greater(double a, double b) { return a > b; }                // TEST AH,0x41 not taken
bool ZeroOrUnordered(double a) { return !(a < 0.0 || a > 0.0); } // TEST AH,0x40 taken

bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }

CItemWeapon* ActiveWeapon(CUnit* unit) { return static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon)); }

// FPATAN with ST1 = y, ST0 = x.
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        fpatan
        ret
    }
}

// The game's _ftol (006E853C): truncation, the low 32 bits of the 64-bit result.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
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
__declspec(naked) double __fastcall UnitGroundHeight(CUnit*, void*, float, float) EI_ORIGINAL(0x00530280)
// 00557B80. CArray::SetSize of an AnimationSteps list.
__declspec(naked) void __fastcall StepsSetSize(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557B80)
// 00455950. The world an object lives in.
__declspec(naked) void* __fastcall ObjectWorld(const void*) EI_ORIGINAL(0x00455950)
// Rewritten elsewhere, called through their game addresses (ST0 results kept unrounded):
// 00530370 CUnit::BaseSpeed (the eye height here), 005302F0 CUnit::ModeSpeed, 0054C070
// CUnitServer::ActionDistance, 005BC3E0 CAIMap::LineOfSight.
__declspec(naked) double __fastcall UnitEyeHeight(CUnit*) EI_ORIGINAL(0x00530370)
__declspec(naked) double __fastcall UnitModeHeight(CUnit*) EI_ORIGINAL(0x005302F0)
__declspec(naked) double __fastcall OrderDistance(CUnitServer*, void*, const UnitAction*) EI_ORIGINAL(0x0054C070)
__declspec(naked) double __fastcall MapLineOfSight(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x005BC3E0)

} // namespace

__declspec(naked) bool CUnitServer::CallCanActFrom(const UnitAction*, const float*, int) EI_ORIGINAL(0x0054C2F0)
__declspec(naked) bool CUnitServer::CallPlanAction(const UnitAction*, OrderPlan*) EI_ORIGINAL(0x0054CA40)
__declspec(naked) bool CUnitServer::CallCanPlanAction(const UnitAction*) EI_ORIGINAL(0x0054C6B0)
__declspec(naked) bool CUnitServer::CallFindSightTarget(const UnitAction*, SightTarget*) EI_ORIGINAL(0x0054BCB0)

// --- What must be seen -----------------------------------------------------------------------

bool CUnitServer::FindSightTarget(const UnitAction* order, SightTarget* out) {
    if (order->flag18) return false;
    void* world = ManagerContext();
    CEngineObject* target = order->target;

    if (order->kind == kOrderCast || order->kind == kOrderCastAt) {
        void* spell;
        if (!OrderSpell(this, order->source, &spell) || !spell) return false;
        if (Get<int>(SpellPrototypeOf(spell), 0x40) == 0) return false;
        out->point[0] = out->point[1] = out->point[2] = 0.0f;
        std::memset(&out->minDistanceSq, 0, sizeof(float));
        out->unit = nullptr;
        if (IsUnit(target)) {
            CUnit* other = static_cast<CUnit*>(target);
            out->unit = other;
            double reach = static_cast<double>(unit->standingHeight) + other->standingHeight + kPointThree;
            out->minDistanceSq = static_cast<float>(reach * reach);
            out->eyeHeight = static_cast<float>(UnitEyeHeight(unit));
            return true;
        }
        std::memcpy(out->point, order->point, sizeof(out->point));
        out->point[2] = static_cast<float>(
            ei::VCall<double>(world, kWorldSurfaceHeight, out->point[0], out->point[1]) + kOne);
        out->eyeHeight = static_cast<float>(UnitEyeHeight(unit));
        return true;
    }

    if (!IsUnit(target)) return false;
    out->point[0] = out->point[1] = out->point[2] = 0.0f;
    out->minDistanceSq = kNearSq;
    out->unit = static_cast<CUnit*>(target);
    out->eyeHeight = static_cast<float>(UnitEyeHeight(unit));
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
    volatile float range = Get<float>(self, 0x904) * Get<float>(self, 0x294);
    float rangeValue = range;
    std::uint32_t chosenBits = LessOrUnordered(rangeValue, limit) ? Bits(limit) : Bits(rangeValue);
    float chosen = std::bit_cast<float>(chosenBits);
    double reach = static_cast<double>(chosen) * kRangeSlack;
    CUnit* unit = self->unit;
    double dy = static_cast<double>(unit->positionY) - point[1];
    double dx = static_cast<double>(unit->positionX) - point[0];
    return LessOrUnordered(reach * reach, dx * dx + dy * dy);
}

// A melee or skill order fails when a living unit other than the target stands between the unit
// and `point` at `time`: within 0.1..0.9 along the way (in units of the squared distance, sic)
// and less than 0.3 to the side.
bool SomeoneInTheWay(CUnitServer* self, const UnitAction* order, const float* point, int time) {
    CUnit* unit = self->unit;
    volatile float dx = point[0] - unit->positionX;
    volatile float dy = point[1] - unit->positionY;
    volatile float dz = point[2] - unit->positionZ;
    double lengthSq = static_cast<double>(dz) * dz + static_cast<double>(dy) * dy;
    lengthSq = lengthSq + static_cast<double>(dx) * dx;
    volatile float ux = static_cast<float>(dx / lengthSq);
    volatile float uy = static_cast<float>(dy / lengthSq);
    volatile float uz = static_cast<float>(dz / lengthSq);
    double negX = -static_cast<double>(ux);
    volatile float side = static_cast<float>(x87::fsqrt(negX * negX + static_cast<double>(uy) * uy));
    volatile float vx = static_cast<float>(uy / side);
    volatile float vy = static_cast<float>(negX / side);
    volatile float vz = static_cast<float>(kZero / side);

    struct Node {
        Node* next;
        Node* prev;
        CEngineObject* object;
    };
    for (Node* node = At<Node*>(self, 0xA78); node;) {
        CEngineObject* other = node->object;
        node = node->next;
        if (other == order->target) continue;
        if (ei::VCall<int>(other, kSlotIsDead) != 0) continue;
        Vector3 scratch;
        const float* p = ei::VCall<float*>(other, kSlotPositionAt, &scratch, time);
        double a = static_cast<double>(p[0]) - unit->positionX;
        double b = static_cast<double>(p[1]) - unit->positionY;
        double c = static_cast<double>(p[2]) - unit->positionZ;
        volatile float along = static_cast<float>(c * uz + b * uy + a * ux);
        volatile float across = static_cast<float>(c * vz + b * vy + a * vx);
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
    float reach = weapon ? weapon->unknown_58 : self->Combat().reach;
    return LessOrUnordered(reach, kMeleeReach);
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
    if (!CallFindSightTarget(order, &sight)) return true;
    if (sight.unit) {
        // A unit target is seen at `point`, at its own height above the ground there.
        std::memcpy(sight.point, point, sizeof(sight.point));
        volatile float ground = static_cast<float>(UnitGroundHeight(sight.unit, nullptr, point[0], point[1]));
        sight.point[2] = static_cast<float>(UnitModeHeight(sight.unit) + ground);
        sight.unit = nullptr;
        // (The original then asks a remaining sight.unit for slot 48; it is always null here.)
    }
    double ey = static_cast<double>(sight.point[1]) - unit->positionY;
    double ex = static_cast<double>(sight.point[0]) - unit->positionX;
    if (LessOrUnordered(ex * ex + ey * ey, sight.minDistanceSq)) return true;

    Vector3 eye;
    eye.x = unit->positionX;
    eye.y = unit->positionY;
    eye.z = unit->positionZ;
    eye.z = static_cast<float>(UnitGroundHeight(unit, nullptr, eye.x, eye.y) + sight.eyeHeight);
    void* map = static_cast<char*>(ManagerContext()) + 0x170;
    double visibility = MapLineOfSight(map, nullptr, &eye, reinterpret_cast<const Vector3*>(sight.point));
    return !LessOrUnordered(visibility, kPointOne);
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
// 15 / stats +0x30 (left at stats +0x30 when that is 0 or NaN), rounded.
int CastValue(CUnitServer* self, void* spell) {
    double scale = Get<float>(self, 0x210);
    if (!ZeroOrUnordered(scale)) scale = kFifteen / scale;
    volatile std::int64_t wide = Get<std::uint32_t>(spell, 0x4C);
    volatile float value = static_cast<float>(static_cast<double>(wide) * scale);
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
    std::memcpy(&origin, &unit->positionX, sizeof(origin));
    int now = At<int>(ObjectWorld(this), 0x64);
    void* spell = nullptr;

    if (order->flag18) {
        double distance = OrderDistance(this, nullptr, order);
        double dy = static_cast<double>(order->point[1]) - unit->positionY;
        double dx = static_cast<double>(order->point[0]) - unit->positionX;
        if (LessOrUnordered(distance * distance, dx * dx + dy * dy)) {
            DestroySteps(&steps);
            return false;
        }
    }

    StepsSetSize(&plan->animation, nullptr, 0, -1);
    plan->unknown_2C = 1;
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
        CallBuildAttackSteps(plan, &steps, &duration, order->target);
    } else if (kind < kOrderCast || kind > kOrderCastAt) {
        CallBuildOrderSteps(&steps, &duration, kind, order->source);
    } else {
        spell = SpellOfSource(order->source);
        int value = 0;
        if (spell) value = CastValue(this, spell);
        CallBuildCastSteps(&steps, &duration, order->kind, value);
    }
    if (duration <= 0) duration = 1;

    volatile float range = static_cast<float>(OrderDistance(this, nullptr, order));
    if (order->flag18) range = std::bit_cast<float>(kNoRangeBits);

    // The unit's heading and how many ticks a half turn takes.
    Vector3 axis;
    reinterpret_cast<const Quaternion*>(&At<float>(unit, 0x34))->AxisY(&axis);
    volatile float facing[3] = {-axis.x, -axis.y, -axis.z};
    volatile float heading = static_cast<float>(Fpatan(facing[1], facing[0]));
    float forward, turnSpeed;
    unit->GetMoveSpeeds(&forward, &turnSpeed);
    volatile float halfTurn = kPi / turnSpeed;
    int ticks = x87::fistp(halfTurn) + 3;

    bool predict = false;
    CUnit* targetUnit = nullptr;
    int castDelay = 0;

    Vector3 aim;
    if (order->target) ei::VCall<float*>(order->target, kSlotAimPoint, &aim);
    else std::memcpy(&aim, order->point, sizeof(aim));
    volatile float delta[3];
    delta[0] = aim.x - origin.x;
    delta[1] = aim.y - origin.y;
    delta[2] = aim.z - origin.z;
    double ox = static_cast<double>(origin.x) - aim.x;
    double oy = static_cast<double>(origin.y) - aim.y;
    volatile float distanceSq = static_cast<float>(oy * oy + ox * ox);

    volatile float reachSq;    // the order's own range, squared
    volatile float outerSq;    // how far the target may be and still be waited for
    if (!IsUnit(order->target)) {
        reachSq = static_cast<float>(static_cast<double>(range) * range);
        outerSq = reachSq;
    } else {
        targetUnit = static_cast<CUnit*>(order->target);
        double speed = ei::VCall<double>(targetUnit, kSlotSpeed);
        reachSq = static_cast<float>(static_cast<double>(range) * range);
        bool inFront = false;
        if (!Greater(distanceSq, reachSq)) {
            double dot = static_cast<double>(delta[2]) * facing[2] + static_cast<double>(facing[1]) * delta[1];
            dot = dot + static_cast<double>(facing[0]) * delta[0];
            double cone = x87::fsqrt(distanceSq) * kFrontCosine;
            inFront = LessOrUnordered(cone, dot);
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
            if (ei::VCall<int>(targetUnit, kSlotIsMoving)) predict = true;
        }
        if ((kind == kOrderCast || kind == kOrderCastAt) && !order->flag18 &&
            ei::VCall<int>(targetUnit, kSlotIsMoving)) {
            // A moving target of a missile spell: aim where it will be when the spell arrives.
            predict = true;
            void* missile;
            if (OrderSpell(this, order->source, &missile) && missile &&
                (Get<std::uint32_t>(missile, 0x3C) & kSpellNoTarget)) {
                float flight = Get<float>(missile, 0x30);
                if (Greater(flight, kZero)) {
                    double travel = x87::fsqrt(distanceSq) * kCastDelayScale;
                    castDelay = GameFtol(travel / flight - kCastDelayBias);
                }
            }
        }
    }

    plan->rangeSq = outerSq;
    float reach = reachSq;
    volatile float best = std::bit_cast<float>(kBestStartBits);
    for (int tick = 0; tick < ticks; ++tick) {
        if (predict) {
            Vector3 scratch;
            const float* p = ei::VCall<float*>(order->target, kSlotPositionAt, &scratch,
                                               tick + castDelay + duration + now);
            std::memcpy(&aim, p, sizeof(aim));
            delta[0] = aim.x - origin.x;
            delta[1] = aim.y - origin.y;
            delta[2] = aim.z - origin.z;
        }
        double squared = static_cast<double>(delta[1]) * delta[1] + static_cast<double>(delta[0]) * delta[0];
        volatile float stored = static_cast<float>(squared);
        bool tryHere = true;
        if (Greater(squared, reach)) {
            // Out of reach: wait while the target comes closer, give up once it moves off.
            if (!Greater(stored, best)) tryHere = false;
            else if (!LessOrUnordered(stored, outerSq)) break;
        }
        if (tryHere) {
            bool facingIt = (ZeroOrUnordered(delta[0]) && ZeroOrUnordered(delta[1])) ||
                            (targetUnit && targetUnit->id_ == unit->id_);
            if (!facingIt) {
                volatile float turn = static_cast<float>(Fpatan(delta[1], delta[0]) - heading);
                turn = std::bit_cast<float>(Bits(turn) & 0x7FFFFFFFu);
                double angle = turn;
                if (Greater(turn, kPi)) angle = static_cast<double>(kPi) + kPi - turn;
                if (!Greater(angle, kPercent)) facingIt = true;
                else if (GameFtol(angle / turnSpeed) + 1 <= tick) facingIt = true;
            }
            if (facingIt) {
                if (!CallCanActFrom(order, &aim.x, tick + duration + now)) break;
                plan->targetId = order->target ? order->target->id_ : -1;
                plan->startTime = tick + now;
                plan->endTime = tick + now + duration;
                StepsSetSize(&plan->animation, nullptr, steps.count, -1);
                for (int i = 0; i < steps.count; ++i) plan->animation.steps[i] = steps.steps[i];
                plan->animation.extra = steps.extra;
                plan->notSkill = order->source != kSkillSource;
                plan->direction[0] = delta[0];
                plan->direction[1] = delta[1];
                plan->direction[2] = delta[2];
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
    bool result = CallPlanAction(order, &plan);
    if (IsUnit(target)) target->flags_ &= ~kFlagPlanned;
    DestroySteps(&plan.animation);
    return result;
}
EI_REPLACE_METHOD(0x0054C6B0, CUnitServer::CanPlanAction);
