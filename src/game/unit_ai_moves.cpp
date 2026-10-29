// The unit AI's movement helpers: the looks planned for a move, a free place to stand
// following a leader and the player mode's path test.
#include "unit_ai.h"

#include "ai_map.h"
#include "combat.h"
#include "core_maths.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "random.h"
#include "script.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include <cstring>
#include "game_singletons.h"
#include "logic_settings.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <list>

namespace {
constexpr float kEyeHeight = 1.4f;                  // looks are tested from this high
constexpr float kWanderScale = 15.0f;
constexpr float kDefaultWander = 40.0f;
constexpr float kMinWaitFraction = 0.25f;
// A random word to an angle in [0, 6.3).
constexpr double kAngleScale = std::bit_cast<double>(0x3E19333340193333ull);
// A random word to [0, 1) (2^-32 and a little).
constexpr double kUnitScale = std::bit_cast<double>(0x3DF0000000100000ull);
constexpr const char kHumanPrefix[] = "unhu";

constexpr float kMaxSlope = 0.3f;
constexpr float kAllySpace = 2.5f;                  // x the standing height
constexpr float kRangePerDistance = 3.0f;
constexpr float kRangeBase = 10.0f;
constexpr float kDetourFactor = 5000.0f;
constexpr float kCooldownPerDistance = 0.33333334f;   // (1 / 3)
static_assert(std::bit_cast<std::uint32_t>(kCooldownPerDistance) == 0x3EAAAAABu);
constexpr int kMaxCooldown = 10;

constexpr int kFollowWait = 8;
constexpr int kFollowPause = 6;
constexpr int kLeaderAhead = 5;                     // ticks ahead of the leader
constexpr int kLeaderFirstStep = 6;
constexpr int kLeaderLastStep = 100;
constexpr int kLeaderSide = 40;
constexpr float kOnTheWaySq = 1.3f;
constexpr float kSideOffset = 1.3649999f;    // (1.365)
static_assert(std::bit_cast<std::uint32_t>(kSideOffset) == 0x3FAEB851u);
constexpr float kReachedSq = 0.1f;
constexpr float kPassRange = 10.0f;
constexpr float kPassTime = 20.0f;

constexpr float kSamePointSq = 0.01f;


// The list the sector map fills.
void InitUnitList(std::list<CUnit*>& list) {
    list = {};
}
} // namespace

void AIBrain::PlanLooks(AIMoveOrder* order, const Vector3* direction, float wander, bool farGoal) {
    std::vector<AILook>& planned = order->looks;
    planned = {};

    IsaacRandom* random = (&g_random);
    int count = static_cast<int>(random->Next() & 3) + 1;
    Vector3 eye = order->point;
    eye.z = static_cast<float>(g_serverWorld.aiMap_.TopHeightAt(&eye) + kEyeHeight);
    if (wander >= 0.0f) wander = static_cast<float>(wander * kWanderScale);
    else wander = kDefaultWander;   // also NaN
    if (farGoal) {
        if (owner->moveMode2.onGuard || owner->unit->name.starts_with(std::string_view(kHumanPrefix, 4))) {
            farGoal = false;
        } else {
            wander = static_cast<float>(static_cast<float>(count) * wander);
            count = 1;
        }
    }

    float minWait = static_cast<float>(wander * kMinWaitFraction);
    for (; count > 0; --count) {
        int tries = 5;
        float dx, dy, wait;
        for (;;) {
            float angle = static_cast<float>(x87::FromUnsigned(random->Next()) * kAngleScale);
            double roll = x87::FromUnsigned(random->Next());
            wait = static_cast<float>((wander - minWait) * roll * kUnitScale + minWait);
            float sine = static_cast<float>(x87::fsin(angle));
            dx = static_cast<float>(x87::fcos(angle) + direction->x);
            dy = static_cast<float>(sine + direction->y);
            if (tries-- < 0) break;
            Vector3 reach{static_cast<float>(dx + dx), static_cast<float>(dy + dy), static_cast<float>(direction->z + direction->z)};
            Vector3 to{static_cast<float>(reach.x + eye.x), static_cast<float>(reach.y + eye.y), static_cast<float>(reach.z + eye.z)};
            float sight = g_serverWorld.aiMap_.LineOfSight(&eye, &to);
            EI_NAN_AUDIT(sight);
            if (sight == 1.0f) break;
        }
        AILook look{};
        look.angle = static_cast<float>(x87::fpatan(dy, dx));
        look.time = 0.0f;
        look.wait = wait;
        look.stopFirst = farGoal;
        lib::SetAtGrow(planned, std::ssize(planned), look);
    }
}

bool AIBrain::FindStandPoint(const Vector3* point, float radius, Vector3* out) {
    if (standCooldown > 0) {
        --standCooldown;
        return false;
    }
    IsaacRandom* random = (&g_random);
    float low = -radius;
    float dx = static_cast<float>((radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low);
    double dy = (radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low;
    float radiusSq = static_cast<float>(radius * radius);
    for (int tries = 9;;) {
        EI_NAN_AUDIT(dy, dx, radiusSq);
        if (LengthSquared2DUnrounded<double>(dy, dx) <= radiusSq) break;
        dx = static_cast<float>((radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low);
        dy = (radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low;
        if (--tries < 0) break;
    }
    out->x = static_cast<float>(dx + point->x);
    out->y = static_cast<float>(dy + point->y);
    out->z = 0.0f;
    CAIMap* map = &g_serverWorld.aiMap_;
    if (map->SlopeAt(out) > kMaxSlope) return false;

    // No unit of an allied player may stand near it.
    std::list<CUnit*> units;
    InitUnitList(units);
    CUnit* self = owner->unit;
    g_serverWorld.aiMap_.sectorMap.FindUnitsInRadius(out, &units, self->standingHeight * kAllySpace);
    for (auto node = units.begin(); node != units.end(); ++node) {
        auto* other = *node;
        if (g_serverWorld.diplomacy_.IsAllied(other->side, self->side)) {
            units.clear();
            return false;
        }
    }

    bool result = true;
    {
        AIPath path;
        double ux = self->localPosition_.x - out->x;
        double uy = self->localPosition_.y - out->y;
        double uz = self->localPosition_.z - out->z;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(ux, uy, uz)));
        float range = static_cast<float>(distance * kRangePerDistance + kRangeBase);
        bool found = map->FindPath(self, out, &path, range, 0.0f);
        // A path much longer than the way there (smoothing) counts as none.
        EI_NAN_AUDIT(distance);
        if (!found || 2.0f * distance * kDetourFactor < static_cast<double>(path.smoothing)) {
            result = false;
            long cooldown = x87::RoundToInt(static_cast<float>(distance * kCooldownPerDistance));
            standCooldown = static_cast<int>(std::min<long>(cooldown, kMaxCooldown));
        }
    }
    units.clear();
    return result;
}

void AIBrain::UpdateFollow() {
    if (!target || target->IsDead()) {
        target.Reset();
        action = kActionNone;
        return;
    }
    if (followTimer++ < kFollowWait) return;

    float farSq = static_cast<float>(g_logicSettings.followMaxDist * g_logicSettings.followMaxDist);
    CUnit* self = owner->unit;
    float nearSq = static_cast<float>(g_logicSettings.followMinDist * g_logicSettings.followMinDist);
    auto* leader = static_cast<CUnit*>(target.Get());
    Vector3 toLeader{static_cast<float>(leader->localPosition_.x - self->localPosition_.x), static_cast<float>(leader->localPosition_.y - self->localPosition_.y),
                     static_cast<float>(leader->localPosition_.z - self->localPosition_.z)};
    int now = g_serverWorld.tick_;
    float distance = static_cast<float>(
        std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(toLeader.x, toLeader.y, toLeader.z)));
    CUnitServer* leaderController = leader->ServerController();
    float walkTime = static_cast<float>(distance * kRangePerDistance + kRangeBase);

    if (leaderController->action.kind != UnitOrderKind::Move && leaderController->action2.kind != UnitOrderKind::Move) {
        // The leader stands: stop near it, else walk to it when the current goal is too far off.
        EI_NAN_AUDIT(distance, nearSq);
        if (static_cast<double>(distance) * distance < nearSq) {
            owner->Idle();
            return;
        }
        Vector3 goal;
        if (!owner->GetMovePoint(&goal.x)) goal = self->localPosition_;
        double gx = goal.x - leader->localPosition_.x;
        double gy = goal.y - leader->localPosition_.y;
        if (LengthSquared2DUnrounded(gx, gy) > farSq) {
            owner->OrderMove(&leader->localPosition_, 0, 0, walkTime);
            followTimer = 0;
        }
        return;
    }

    // The leader walks: where it will be in a moment.
    Vector3 goal = self->localPosition_;
    owner->GetMovePoint(&goal.x);
    Vector3 soon;
    leader->PositionAt(&soon, now + kLeaderAhead);
    double sx = self->localPosition_.x - soon.x;
    double sy = self->localPosition_.y - soon.y;
    double soonSq = LengthSquared2DUnrounded(sx, sy);
    if (soonSq >= static_cast<double>(distance) * distance || soonSq >= nearSq) {
        // Not coming closer: walk to that point unless already going near it.
        double fx = soon.x - goal.x;
        double fy = soon.y - goal.y;
        if (LengthSquared2DUnrounded(fy, fx) > farSq) {
            owner->OrderMove(&soon, 0, 0, walkTime);
            followTimer = kFollowPause;
        }
        return;
    }

    // The leader comes this way: find the moment it passes closest; when it passes within reach,
    // step aside of its way.
    float best = static_cast<float>(soonSq);
    for (int ahead = kLeaderFirstStep;; ) {
        Vector3 later;
        leader->PositionAt(&later, now + ahead);
        double lx = self->localPosition_.x - later.x;
        double ly = self->localPosition_.y - later.y;
        double laterSq = LengthSquared2DUnrounded(lx, ly);
        if (laterSq > best) return;
        EI_NAN_AUDIT(laterSq);
        if (laterSq < kOnTheWaySq) break;
        best = static_cast<float>(laterSq);
        if (++ahead >= kLeaderLastStep) return;
    }

    Vector3 passing;
    leader->PositionAt(&passing, now + kLeaderSide);
    double along = passing.y - leader->localPosition_.y;
    float across = static_cast<float>(passing.x - leader->localPosition_.x);
    Vector3 side{static_cast<float>(-along), across, 0.0f};
    double lengthSq = static_cast<double>(side.y) * side.y + along * along;
    EI_NAN_AUDIT(lengthSq);
    if (lengthSq <= 0.0) return;
    double scale = kSideOffset / std::sqrt(lengthSq);
    side.x = static_cast<float>(scale * -along);
    side.y = static_cast<float>(scale * side.y);
    side.z = static_cast<float>(side.z * scale);
    double ux = self->localPosition_.x - leader->localPosition_.x;
    double uy = self->localPosition_.y - leader->localPosition_.y;
    double uz = self->localPosition_.z - leader->localPosition_.z;
    EI_NAN_AUDIT(side.x, side.y, side.z, ux, uy, uz);
    if (DotUnrounded<Sum3::ZYX, double>(side.x, side.y, side.z, ux, uy, uz) < 0.0) {
        side.x = -side.x;
        side.y = -side.y;
        side.z = -side.z;
    }

    AIPath path;
    Vector3 aside{static_cast<float>(passing.x + side.x), static_cast<float>(passing.y + side.y), static_cast<float>(side.z + passing.z)};
    g_serverWorld.aiMap_.FindPath(self, &aside, &path, kPassRange, 0.0f);
    double half = 0.5;
    float targetY = static_cast<float>(half * path.targetY);
    float offY = static_cast<float>(aside.y - targetY);
    double offX = aside.x - half * path.targetX;
    EI_NAN_AUDIT(offX, offY);
    if (LengthSquared2DUnrounded<double>(offX, offY) < kReachedSq) {
        owner->OrderMove(&aside, 0, 0, kPassRange);
        followTimer = kFollowPause;
    } else {
        double px = goal.x - passing.x;
        double py = goal.y - passing.y;
        if (LengthSquared2DUnrounded(px, py) > 0.5f) {
            owner->OrderMove(&passing, 0, 0, kPassTime);
            followTimer = kFollowPause;
        }
    }
}

bool PlayerMode::PathClear(AIBrain* brain, const Vector3* from, const Vector3* to) {
    double fy = from->y - to->y;
    double fx = from->x - to->x;
    EI_NAN_AUDIT(fx, fy);
    if (LengthSquared2DUnrounded(fx, fy) < kSamePointSq) return false;
    AIPath path;
    double dx = from->x - to->x;
    double dy = from->y - to->y;
    double dz = from->z - to->z;
    CUnit* unit = brain->owner->unit;
    Vector3 saved = unit->localPosition_;
    float range = static_cast<float>(2.0 * std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
    g_serverWorld.aiMap_.sectorMap.Move(unit, from);
    bool found = g_serverWorld.aiMap_.FindPath(unit, to, &path, range, 0.0f);
    g_serverWorld.aiMap_.sectorMap.Move(unit, &saved);
    return found;
}
