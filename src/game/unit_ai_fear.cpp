// The unit AI's Fear mode: how afraid the unit is (Evaluate) and running
// away or taking shelter (Execute).
#include "ai_map.h"
#include "combat.h"
#include "game_singletons.h"
#include "logic_settings.h"
#include "map_object.h"
#include "random.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kFlee = 15000;
constexpr int kSeesEnemy = 3000;
constexpr int kShelter = 350;
constexpr int kCalmLevel = 50;              // noticed levels above it keep the unit afraid
constexpr int kKeepLevel = 500;             // a watchful mode does not forget levels above it
constexpr int kLargeSizeClass = 6;

constexpr float kThree = 3.0f;
constexpr float kThird = 0.33333334f;
static_assert(std::bit_cast<std::uint32_t>(kThird) == 0x3EAAAAABu);
constexpr float kSeenScale = 1.5f;
constexpr float kMaxLoadFactor = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kMaxLoadFactor) == 0x3F19999Au);
constexpr float kFlightStep = 5.0f;
constexpr float kRangeBase = 10.0f;
constexpr float kSmoothingScale = 5000.0f;
constexpr float kFar = 1e30f;
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CAu);
constexpr float kDistanceWeight = 30.0f;
constexpr float kDistanceDivisor = 0.032258064f; // (1 / 31)
static_assert(std::bit_cast<std::uint32_t>(kDistanceDivisor) == 0x3D042108u);
constexpr float kSpreadScale = 2.5f;
constexpr float kMinDistance = 3.0f;
constexpr float kNoWander = -1.0f;
constexpr float kMoveTime = 1000000.0f;
constexpr double kRandomSix = std::bit_cast<double>(0x3E18000000180000ull);   // 6 / 2^32
constexpr double kRandomUnit = std::bit_cast<double>(0x3DF0000000100000ull);  // 1 / 2^32

bool IsZero(double v) {
    EI_NAN_AUDIT(v);
    return v == 0.0;
}

bool IsUnset(const Vector3& point) { return point.x == kUnsetCoordinate; }

// A flight point 5 from the enemy's side of the unit and within 3 of it at random, accepted when a
// path there is not too roundabout and the order check from the path's end (at most 1 from the
// point) lets the unit attack `enemy` from there.
bool FindFlightPoint(AIBrain* brain, CUnit* self, CUnit* enemy, Vector3 me, const Vector3& foe, float dx, float dy,
                     Vector3* out) {
    Vector3 away{dx, dy, static_cast<float>(static_cast<double>(foe.z) - me.z)};
    double length = LengthZXY(away);
    if (!IsZero(length)) {
        double scale = kFlightStep / length;
        away.x = static_cast<float>(scale * away.x);
        away.y = static_cast<float>(away.y * scale);
        away.z = static_cast<float>(away.z * scale);
    }
    IsaacRandom& random = g_random;
    float jitterX = static_cast<float>(x87::FromUnsigned(random.Next()) * kRandomSix - kThree);
    float jitterY = static_cast<float>(x87::FromUnsigned(random.Next()) * kRandomSix - kThree);
    Vector3 back = me - away;
    Vector3 dest{static_cast<float>(static_cast<double>(back.x) + jitterX), static_cast<float>(static_cast<double>(back.y) + jitterY), back.z};

    AIPath path;
    double way = DistanceZXY(dest, me);
    float wayRounded = static_cast<float>(way);
    float range = static_cast<float>(way * kThree + kRangeBase);
    if (!g_serverWorld.aiMap_.FindPath(self, dest, &path, range, kMinDistance)) return false;
    double scale = 0.5;
    double limit = 1.0f / scale * wayRounded * kSmoothingScale;
    EI_NAN_AUDIT(limit);
    if (limit < static_cast<double>(path.smoothing)) return false;
    Vector3 end{static_cast<float>(scale * path.targetX), static_cast<float>(scale * path.targetY), 0.0f};

    AIOrder order;
    order.MakeAttack(enemy);
    if (!brain->owner->CanReachAndPlanFrom(&order, end)) return false;
    double ex = static_cast<double>(dest.x) - end.x;
    double ey = static_cast<double>(dest.y) - end.y;
    double ez = static_cast<double>(dest.z) - end.z;
    EI_NAN_AUDIT(ex, ey, ez);
    if (LengthSquaredUnrounded<Sum3::ZYX>(ex, ey, ez) >= 1.0f) return false;
    *out = dest;
    return true;
}
} // namespace

int FearMode::Evaluate(AIBrain* brain) {
    CUnitServer* owner = brain->owner;
    CUnit* self = owner->unit;
    // The sum rounded first, as the main thread's 24-bit FPU does (under /fp:fast MSVC
    // otherwise distributes the product, spread * 2/3 + 1, and the result drifts by an ulp).
    float grown = static_cast<float>(static_cast<double>(spread) + spread + kThree);
    spread = static_cast<float>(static_cast<double>(grown) * kThird);
    if (CMapObject* seen = brain->seen) {
        const Vector3* at = &seen->localPosition_;
        brain->memory.Notice(*at, kNoticeSeenLevel, kNoticeSeenDecay);
        const Vector3* me = &brain->owner->unit->localPosition_;
        float x = static_cast<float>(static_cast<double>(me->x) - at->x);
        float y = static_cast<float>(static_cast<double>(me->y) - at->y);
        float z = static_cast<float>(static_cast<double>(me->z) - at->z);
        float reach = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZXY, double>(x, y, z)) * kSeenScale);
        // The larger of the two; an unordered pair takes the new one.
        distance = distance >= reach ? distance : reach;
    }

    // A unit that fights from a distance keeps away from its nearest enemy.
    CUnit* enemy = brain->memory.NearestEnemy(self);
    if (enemy && static_cast<std::uint8_t>(owner->AttacksFromRange()) != 0 &&
        self->sizeClass < kLargeSizeClass && owner->unit->GetSpellObject(0) == nullptr &&
        owner->stats.MovementState(0) == 0 && owner->stats.loadFactor > kMaxLoadFactor) {
        bool aggressive = brain->current && brain->current->type == AIModeType::Aggression;
        float range = aggressive ? g_logicSettings.rangedRunDist : g_logicSettings.rangedAttackDist;
        Vector3 me = self->localPosition_;
        Vector3 foe = enemy->localPosition_;
        double dx = static_cast<double>(foe.x) - me.x;
        double dy = static_cast<double>(foe.y) - me.y;
        if (static_cast<double>(range) * range > LengthSquared2DUnrounded(dx, dy)) {
            if (brain->owner->action.kind == UnitOrderKind::Move || brain->owner->action2.kind == UnitOrderKind::Move) return kFlee;
            Vector3 flight;
            if (FindFlightPoint(brain, self, enemy, me, foe, static_cast<float>(dx), static_cast<float>(dy), &flight)) {
                fleePoint = flight;
                fleePoint.z = 1.0f;
                return kFlee;
            }
        }
    }

    if (brain->memory.seesEnemy) return kSeesEnemy;
    const Vector3* here = &self->localPosition_;
    float height = self->GetPrototype()->height;
    EI_NAN_AUDIT(height);
    if (g_serverWorld.aiMap_.HasSpellAreaAt(*here) && (self->sizeClass != 0 || height < 0.5f)) {
        shelter = *here;
        return kShelter;
    }
    if (!IsUnset(shelter) && g_serverWorld.aiMap_.HasSpellAreaAt(shelter)) return kShelter;

    if (watchful) {
        // The nearest player unit in sight (2D).
        double best = kFar;
        Vector3 nearest{};      // left unset when there is none (then 1e30 is never inside)
        for (auto& node : brain->memory.visible) {
            CUnit* other = node;
            if (!other->player) continue;
            const Vector3* me = &brain->owner->unit->localPosition_;
            double oy = static_cast<double>(me->y) - other->localPosition_.y;
            double ox = static_cast<double>(me->x) - other->localPosition_.x;
            double d2 = LengthSquared2DUnrounded(ox, oy);
            if (d2 < best) {
                nearest = other->localPosition_;
                best = static_cast<float>(d2);
            }
        }
        if (best < static_cast<double>(distance) * distance)
            brain->memory.Notice(nearest, kNoticeSeenLevel, kNoticeSeenDecay);
    }
    shelter.x = kUnsetCoordinate;

    if (brain->memory.noticedLevel > kCalmLevel) {
        if (brain->current == this) return brain->memory.noticedLevel / 6;
        const Vector3* me = &brain->owner->unit->localPosition_;
        double nx = static_cast<double>(me->x) - brain->memory.noticedX;
        double ny = static_cast<double>(me->y) - brain->memory.noticedY;
        double z = me->z;   // not a difference: the memory keeps no z
        if (static_cast<double>(distance) * distance > z * z + ny * ny + nx * nx) return brain->memory.noticedLevel / 6;
    }
    // Back towards the default by 1/31 a call; the sum rounded first (see spread above: distributed,
    // 30/31 * distance + default / 31 settled at 9.99999714 instead of 10).
    float weighted = static_cast<float>(static_cast<double>(distance) * kDistanceWeight + kDefaultDistance);
    distance = static_cast<float>(static_cast<double>(weighted) * kDistanceDivisor);
    return kPriorityNone;
}

void FearMode::Execute(AIBrain* brain) {
    AIMoveOrder order;
    CUnitServer* owner = brain->owner;
    const Vector3* here = &owner->unit->localPosition_;

    // What to run from: the nearest enemy, a shelter to stay in, or the noticed point.
    Vector3 from{brain->memory.noticedX, brain->memory.noticedY, 0.0f};
    if (brain->memory.seesEnemy) {
        if (CUnit* enemy = brain->memory.NearestEnemy(brain->owner->unit)) {
            from = enemy->localPosition_;
            brain->CallAllies(enemy->localPosition_);
        }
    } else if (g_serverWorld.aiMap_.HasSpellAreaAt(*here)) {
        from = *here;
    } else if (!IsUnset(shelter) && g_serverWorld.aiMap_.HasSpellAreaAt(shelter)) {
        from = shelter;
    } else if (!watchful || brain->memory.noticedLevel <= kKeepLevel) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedDecay = 0;
    }

    double near2 = DistanceSquaredZYX(*here, from);
    if (static_cast<double>(distance) * distance > near2 && brain->owner->action.kind != UnitOrderKind::Move &&
        brain->owner->action2.kind != UnitOrderKind::Move) {
        Vector3 dest;
        if (!IsZero(fleePoint.z)) {
            dest = fleePoint;
            fleePoint.z = 0.0f;
        } else {
            double vx = static_cast<double>(from.x) - here->x;
            double vy = static_cast<double>(from.y) - here->y;
            double vz = static_cast<double>(from.z) - here->z;
            Vector3 step{static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)};
            float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(vx, vy, vz)));
            float limit = static_cast<float>(static_cast<double>(distance) * kSpreadScale);
            // The smaller of the two.
            EI_NAN_AUDIT(spread, limit);
            float width = spread < limit ? spread : limit;
            spread = width;
            if (!IsZero(length)) {
                double scale = kFlightStep / length;
                step.x = static_cast<float>(scale * step.x);
                step.y = static_cast<float>(step.y * scale);
                step.z = static_cast<float>(step.z * scale);
            }
            float low = static_cast<float>(-static_cast<double>(width));
            IsaacRandom& random = g_random;
            float jitterX = static_cast<float>(x87::FromUnsigned(random.Next()) * (static_cast<double>(width) - low) * kRandomUnit + low);
            double jitterY = x87::FromUnsigned(random.Next()) * (static_cast<double>(width) - low) * kRandomUnit + low;
            float backZ = static_cast<float>(static_cast<double>(here->z) - step.z);
            double backY = static_cast<double>(here->y) - step.y;
            float backX = static_cast<float>(static_cast<double>(here->x) - step.x);
            dest.x = static_cast<float>(static_cast<double>(backX) + jitterX);
            dest.y = static_cast<float>(backY + jitterY);
            dest.z = backZ;
        }
        AIPath path;
        double way = DistanceZYX(dest, *here);
        float range = static_cast<float>(way * kThree + kRangeBase);
        if (g_serverWorld.aiMap_.FindPath(brain->owner->unit, dest, &path, range, kMinDistance)) {
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(dest, 0, 0, kMoveTime);
        }
        spread = static_cast<float>(static_cast<double>(spread) + spread);
    }

    if (brain->action == AIBrain::kActionNone) {
        // Fight back when there is something worth it (helping allies too).
        UnitList candidates;
        Vector3 at = brain->owner->unit->localPosition_;
        AITargetChoice best = brain->Choose(&candidates, at, true, true);
        brain->ApplyAttack(&best);

        if (brain->action == AIBrain::kActionNone) {
            // Stand still and look away from the danger.
            order.point = brain->owner->unit->localPosition_;
            Vector3 v = from - *here;
            Vector3 direction = v;
            double length = LengthXYZ(v);
            if (!IsZero(length)) {
                double twice = length + length;
                direction.x = static_cast<float>(v.x / twice);
                direction.y = static_cast<float>(v.y / twice);
                direction.z = static_cast<float>(v.z / twice);
            }
            brain->PlanLooks(&order, direction, kNoWander, false);
            brain->StartMove(&order);
        }
    }
}
