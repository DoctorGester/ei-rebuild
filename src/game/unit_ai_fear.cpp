// The unit AI's Fear mode (vtable 0073FF8C): how afraid the unit is (Evaluate, 005D2960) and running
// away or taking shelter (Execute, 005D30A0); and the brain taking up a chosen attack (005CF170).
#include "unit_ai.h"

#include "ai_map.h"
#include "ai_scoped_path.h"
#include "combat.h"
#include "logic_settings.h"
#include "map_object.h"
#include "random.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace {
constexpr int kFlee = 15000;
constexpr int kSeesEnemy = 3000;
constexpr int kShelter = 350;
constexpr int kNoPriority = -1000;
constexpr int kNoticeLevel = 1000;
constexpr int kNoticeKind = -2;
constexpr int kCalmLevel = 50;              // noticed levels above it keep the unit afraid
constexpr int kKeepLevel = 500;             // with `flag`, levels above it are not forgotten
constexpr int kCommandWalk = 1;             // CUnitServer::command / nextCommand
constexpr int kLargeSizeClass = 6;

constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kHalf = 0.5f;                                       // 0073B86C
constexpr float kOne = 1.0f;                                        // 0073B84C, 0073FEC8
constexpr float kThree = 3.0f;                                      // 0073BAA4
constexpr float kThird = std::bit_cast<float>(0x3EAAAAABu);         // 0073D9A4
constexpr float kSeenScale = 1.5f;                                  // 0073C51C
constexpr float kMaxLoadFactor = std::bit_cast<float>(0x3F19999Au); // 0073CDE0 (0.6)
constexpr float kFlightStep = 5.0f;                                 // 0073BF34
constexpr float kRangeBase = 10.0f;                                 // 0073BAAC
constexpr float kSmoothingScale = 5000.0f;                          // 0073FFC4
constexpr float kFar = std::bit_cast<float>(0x7149F2CAu);           // 0073E73C (1e30)
constexpr float kDistanceWeight = 30.0f;                            // 0073BAF0
constexpr float kDistanceDivisor = std::bit_cast<float>(0x3D042108u); // 0073FFC0 (1 / 31)
constexpr float kSpreadScale = 2.5f;                                // 0073CE18
constexpr float kMinDistance = 3.0f;                                // pushed as 0x40400000
constexpr float kNoWander = -1.0f;                                  // pushed as 0xBF800000
constexpr float kMoveTime = 1000000.0f;                             // pushed as 0x49742400
constexpr double kRandomSix = std::bit_cast<double>(0x3E18000000180000ull);   // 0073FFC8: 6 / 2^32
constexpr double kRandomUnit = std::bit_cast<double>(0x3DF0000000100000ull);  // 0073C468: 1 / 2^32
constexpr std::uint32_t kUnsetBits = 0xC4A42000u;                   // -1313: no shelter

// FCOMP 0; TEST AH,0x40; JNZ: equal to zero or unordered.
bool ZeroOrNaN(double v) { return x87::IsNaN(static_cast<float>(v)) || v == 0.0; }

bool IsUnset(const Vector3& point) { return std::bit_cast<std::uint32_t>(point.x) == kUnsetBits; }

const Vector3* PositionOf(CEngineObject* object) {
    return &static_cast<CMapObject*>(object)->position;
}

CAIMap* Map() { return WorldServer()->AIMap(); }

// The prototype's +0x24 (units below 0.5 count as small).
float PrototypeSize(CUnit* unit) {
    return std::bit_cast<float>(reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype())->height);
}

// The original builds the move order's looks with the derived array constructor (005D8B40), then
// clears the order (00545B20).
void ConstructOrder(AIMoveOrder* order) {
    LooksInit(&order->looks);
    order->looks.vtable = mfc::impl::AiLookRecordArrayClass::Vtable();
    order->Reset();
}

// A flight point 5 from the enemy's side of the unit and within 3 of it at random, accepted when a
// path there is not too roundabout and the order check from the path's end (at most 1 from the
// point) lets the unit attack `enemy` from there.
bool FindFlightPoint(AIBrain* brain, CUnit* self, CUnit* enemy, Vector3 me, const Vector3& foe, float dx, float dy,
                     Vector3* out) {
    Vector3 away{dx, dy, static_cast<float>(static_cast<double>(foe.z) - me.z)};
    double length = LengthZXY(away);
    if (!ZeroOrNaN(length)) {
        double scale = kFlightStep / length;
        away.x = static_cast<float>(scale * away.x);
        away.y = static_cast<float>(away.y * scale);
        away.z = static_cast<float>(away.z * scale);
    }
    IsaacRandom& random = IsaacRandom::Global();
    float jitterX = static_cast<float>(x87::FromUnsigned(random.Next()) * kRandomSix - kThree);
    float jitterY = static_cast<float>(x87::FromUnsigned(random.Next()) * kRandomSix - kThree);
    Vector3 back = me - away;
    Vector3 dest{static_cast<float>(static_cast<double>(back.x) + jitterX), static_cast<float>(static_cast<double>(back.y) + jitterY), back.z};

    ScopedPath local;
    double way = DistanceZXY(dest, me);
    float wayRounded = static_cast<float>(way);
    float range = static_cast<float>(way * kThree + kRangeBase);
    if (!Map()->FindPath(self, &dest, &local.path, range, kMinDistance)) return false;
    double scale = static_cast<double>(kOne) * kHalf;
    double limit = kOne / scale * wayRounded * kSmoothingScale;
    if (!(limit >= static_cast<double>(local.path.smoothing))) return false;
    Vector3 end{static_cast<float>(scale * local.path.targetX), static_cast<float>(scale * local.path.targetY), 0.0f};

    AIOrder order;
    order.object = nullptr;
    order.Clear();
    order.MakeAttack(enemy);
    bool allowed = brain->owner->CanReachAndPlanFrom(AsUnitAction(&order), &end);
    reinterpret_cast<AIRef*>(&order.object)->Reset();
    if (!allowed) return false;
    double ex = static_cast<double>(dest.x) - end.x;
    double ey = static_cast<double>(dest.y) - end.y;
    double ez = static_cast<double>(dest.z) - end.z;
    if (!(LengthSquaredUnrounded<Sum3::ZYX>(ex, ey, ez) < kOne)) return false;
    *out = dest;
    return true;
}
} // namespace

int FearMode::Evaluate(AIBrain* brain) {
    CUnitServer* owner = brain->owner;
    CUnit* self = owner->unit;
    // The sum rounded first, as the main thread's 24-bit FPU does (SV7d1: under /fp:fast MSVC
    // otherwise distributes the product, spread * 2/3 + 1, and the result drifts by an ulp).
    float grown = static_cast<float>(static_cast<double>(spread) + spread + kThree);
    spread = static_cast<float>(static_cast<double>(grown) * kThird);
    if (CEngineObject* seen = brain->seen) {
        const Vector3* at = PositionOf(seen);
        brain->Notice(at, kNoticeLevel, kNoticeKind);
        const Vector3* me = PositionOf(brain->owner->unit);
        float x = static_cast<float>(static_cast<double>(me->x) - at->x);
        float y = static_cast<float>(static_cast<double>(me->y) - at->y);
        float z = static_cast<float>(static_cast<double>(me->z) - at->z);
        float reach = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZXY, double>(x, y, z)) * kSeenScale);
        // The larger of the two; an unordered pair takes the new one.
        if (!(distance >= reach)) distance = reach;
    }

    // A unit that fights from a distance keeps away from its nearest enemy.
    CUnit* enemy = brain->NearestEnemy(self);
    if (enemy && static_cast<std::uint8_t>(reinterpret_cast<CUnitServer*>(owner)->AttacksFromRange()) != 0 &&
        self->sizeClass < kLargeSizeClass && owner->unit->GetSpellObject(0) == nullptr &&
        owner->stats.MovementState(0) == 0 && owner->stats.loadFactor > kMaxLoadFactor) {
        bool aggressive = brain->current && brain->current->GetType() == kModeAggression;
        float range = aggressive ? GameLogicSettings().rangedRunDist : GameLogicSettings().rangedAttackDist;
        Vector3 me = *PositionOf(self);
        Vector3 foe = *PositionOf(enemy);
        double dx = static_cast<double>(foe.x) - me.x;
        double dy = static_cast<double>(foe.y) - me.y;
        if (static_cast<double>(range) * range > LengthSquared2DUnrounded(dx, dy)) {
            if (brain->owner->action.kind == kCommandWalk || brain->owner->action2.kind == kCommandWalk) return kFlee;
            Vector3 flight;
            if (FindFlightPoint(brain, self, enemy, me, foe, static_cast<float>(dx), static_cast<float>(dy), &flight)) {
                fleePoint = flight;
                fleePoint.z = kOne;
                return kFlee;
            }
        }
    }

    if (brain->seesEnemy) return kSeesEnemy;
    const Vector3* here = PositionOf(self);
    if (Map()->HasKind3OccluderAt(here) && (self->sizeClass != 0 || !(PrototypeSize(self) >= kHalf))) {
        shelter = *here;
        return kShelter;
    }
    if (!IsUnset(shelter) && Map()->HasKind3OccluderAt(&shelter)) return kShelter;

    if (flag) {
        // The nearest player unit in sight (2D).
        double best = kFar;
        Vector3 nearest{};      // the original leaves it unset when there is none (then 1e30 is never inside)
        for (auto* node = brain->visible.head; node; node = node->next) {
            auto* other = static_cast<CUnit*>(node->data);
            if (!other->player) continue;
            const Vector3* me = PositionOf(brain->owner->unit);
            double oy = static_cast<double>(me->y) - other->position.y;
            double ox = static_cast<double>(me->x) - other->position.x;
            double d2 = LengthSquared2DUnrounded(ox, oy);
            if (d2 < best) {
                nearest = *PositionOf(other);
                best = static_cast<float>(d2);
            }
        }
        if (best < static_cast<double>(distance) * distance) brain->Notice(&nearest, kNoticeLevel, kNoticeKind);
    }
    shelter.x = std::bit_cast<float>(kUnsetBits);

    if (brain->noticedLevel > kCalmLevel) {
        if (brain->current == this) return brain->noticedLevel / 6;
        const Vector3* me = PositionOf(brain->owner->unit);
        double nx = static_cast<double>(me->x) - brain->noticedX;
        double ny = static_cast<double>(me->y) - brain->noticedY;
        double z = me->z;   // not a difference: the memory keeps no z
        if (static_cast<double>(distance) * distance > z * z + ny * ny + nx * nx) return brain->noticedLevel / 6;
    }
    // Back towards the default by 1/31 a call; the sum rounded first (see spread above: distributed,
    // 30/31 * distance + default / 31 settled at 9.99999714 instead of 10).
    float weighted = static_cast<float>(static_cast<double>(distance) * kDistanceWeight + FearDefaultDistance());
    distance = static_cast<float>(static_cast<double>(weighted) * kDistanceDivisor);
    return kNoPriority;
}

EI_REPLACE_METHOD(0x005D2960, FearMode::Evaluate);

void FearMode::Execute(AIBrain* brain) {
    AIMoveOrder order;
    ConstructOrder(&order);
    CUnitServer* owner = brain->owner;
    const Vector3* here = PositionOf(owner->unit);

    // What to run from: the nearest enemy, a shelter to stay in, or the noticed point.
    Vector3 from{brain->noticedX, brain->noticedY, 0.0f};
    if (brain->seesEnemy) {
        if (CUnit* enemy = brain->NearestEnemy(brain->owner->unit)) {
            from = *PositionOf(enemy);
            brain->CallAllies(PositionOf(enemy));
        }
    } else if (Map()->HasKind3OccluderAt(here)) {
        from = *here;
    } else if (!IsUnset(shelter) && Map()->HasKind3OccluderAt(&shelter)) {
        from = shelter;
    } else if (!flag || brain->noticedLevel <= kKeepLevel) {
        brain->noticedLevel = 0;
        brain->noticedKind = 0;
    }

    double near2 = DistanceSquaredZYX(*here, from);
    if (static_cast<double>(distance) * distance > near2 && brain->owner->action.kind != kCommandWalk &&
        brain->owner->action2.kind != kCommandWalk) {
        Vector3 dest;
        if (!ZeroOrNaN(fleePoint.z)) {
            dest = fleePoint;
            fleePoint.z = 0.0f;
        } else {
            double vx = static_cast<double>(from.x) - here->x;
            double vy = static_cast<double>(from.y) - here->y;
            double vz = static_cast<double>(from.z) - here->z;
            Vector3 step{static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)};
            float length = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ>(vx, vy, vz)));
            float limit = static_cast<float>(static_cast<double>(distance) * kSpreadScale);
            // The smaller of the two; an unordered pair keeps the old spread.
            float width = (spread < limit || x87::IsNaN(spread) || x87::IsNaN(limit)) ? spread : limit;
            spread = width;
            if (!ZeroOrNaN(length)) {
                double scale = kFlightStep / length;
                step.x = static_cast<float>(scale * step.x);
                step.y = static_cast<float>(step.y * scale);
                step.z = static_cast<float>(step.z * scale);
            }
            float low = static_cast<float>(-static_cast<double>(width));
            IsaacRandom& random = IsaacRandom::Global();
            float jitterX = static_cast<float>(x87::FromUnsigned(random.Next()) * (static_cast<double>(width) - low) * kRandomUnit + low);
            double jitterY = x87::FromUnsigned(random.Next()) * (static_cast<double>(width) - low) * kRandomUnit + low;
            float backZ = static_cast<float>(static_cast<double>(here->z) - step.z);
            double backY = static_cast<double>(here->y) - step.y;
            float backX = static_cast<float>(static_cast<double>(here->x) - step.x);
            dest.x = static_cast<float>(static_cast<double>(backX) + jitterX);
            dest.y = static_cast<float>(backY + jitterY);
            dest.z = backZ;
        }
        ScopedPath local;
        double way = DistanceZYX(dest, *here);
        float range = static_cast<float>(way * kThree + kRangeBase);
        if (Map()->FindPath(brain->owner->unit, &dest, &local.path, range, kMinDistance)) {
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(&dest.x, 0, 0, kMoveTime);
        }
        spread = static_cast<float>(static_cast<double>(spread) + spread);
    }

    if (brain->action == AIBrain::kActionNone) {
        // Fight back when there is something worth it (helping allies too).
        AIObjectList candidates{};
        candidates.vtable = mfc::PtrListVtable();
        candidates.blockSize = 10;
        int best[2] = {-1, 0};      // {part, object}
        Vector3 at = *PositionOf(brain->owner->unit);
        brain->ChooseTarget(best, &candidates, &at, 1, 1);
        if (best[1]) brain->ApplyAttack(best);
        candidates.RemoveAll();

        if (brain->action == AIBrain::kActionNone) {
            // Stand still and look away from the danger.
            order.point = *PositionOf(brain->owner->unit);
            Vector3 v = from - *here;
            Vector3 direction = v;
            double length = LengthXYZ(v);
            if (!ZeroOrNaN(length)) {
                double twice = length + length;
                direction.x = static_cast<float>(v.x / twice);
                direction.y = static_cast<float>(v.y / twice);
                direction.z = static_cast<float>(v.z / twice);
            }
            brain->PlanLooks(&order, &direction, kNoWander, false);
            reinterpret_cast<AIRef*>(&brain->target)->Assign(nullptr);
            brain->action = AIBrain::kActionMove;
            brain->actionTime = AIBrain::kNoTime;
            // 005D7970: the order assigned to the brain's action point and looks.
            brain->actionPoint = order.point;
            brain->looks.Copy(order.looks);
            brain->owner->OrderMove(&brain->actionPoint.x, 0, 0, kMoveTime);
        }
    }
    LooksDestroyAt(&order.looks);
}

EI_REPLACE_METHOD(0x005D30A0, FearMode::Execute);

void AIBrain::ApplyAttack(const int* partAndObject) {
    auto* object = reinterpret_cast<CEngineObject*>(partAndObject[1]);
    if (!object) return;
    int now[2] = {-1, 0};
    CurrentAttack(now);
    if (object == reinterpret_cast<CEngineObject*>(now[1]) && partAndObject[0] == now[0]) return;
    int part = partAndObject[0];
    if (part == -1) {
        AttackTarget(object);
        return;
    }
    if (!owner->IsScrollSource(part)) action = kActionCast;
    if (object != target) {
        if (target) target->Release();
        target = nullptr;
        ++object->refCount_;
        target = object;
    }
    actionTime = part;
    owner->OrderCast(part, object, 0);
}

void __fastcall ApplyAttackHook(AIAttention* attention, void*, AIBrain* brain, const int* partAndObject) {
    (void)attention;    // always the brain's +0xA4
    brain->ApplyAttack(partAndObject);
}
EI_REPLACE(0x005CF170, ApplyAttackHook);
