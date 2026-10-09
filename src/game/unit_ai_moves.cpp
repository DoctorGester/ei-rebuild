// The unit AI's movement helpers: the looks planned for a move (005D5B40), a free place to stand
// (005D7F80), following a leader (005D6F90) and the player mode's path test (005D37E0).
#include "unit_ai.h"

#include "ai_map.h"
#include "ai_scoped_path.h"
#include "combat.h"
#include "core_maths.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "random.h"
#include "script.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace {
constexpr float kEyeHeight = 1.4f;                  // 0073CDF8: looks are tested from this high
constexpr float kWanderScale = 15.0f;               // 0073C518
constexpr float kDefaultWander = 40.0f;
constexpr float kMinWaitFraction = 0.25f;           // 0073C3B0
// 0073FFD0: a random word to an angle in [0, 6.3).
constexpr double kAngleScale = std::bit_cast<double>(0x3E19333340193333ull);
// 0073C468: a random word to [0, 1) (2^-32 and a little).
constexpr double kUnitScale = std::bit_cast<double>(0x3DF0000000100000ull);
constexpr const char kHumanPrefix[] = "unhu";       // 00791EC4

constexpr float kMaxSlope = 0.3f;                   // 0073C54C
constexpr float kAllySpace = 2.5f;                  // 0073CE18 x the standing height
constexpr float kRangePerDistance = 3.0f;           // 0073BAA4
constexpr float kRangeBase = 10.0f;                 // 0073BAAC
constexpr float kOne = 1.0f;                        // 0073FEC8 / 0073B84C
constexpr float kHalf = 0.5f;                       // 0073B86C: half-cells to world units
constexpr float kDetourFactor = 5000.0f;            // 0073FFC4
constexpr float kCooldownPerDistance = std::bit_cast<float>(0x3EAAAAABu);   // 0073D9A4 (1 / 3)
constexpr int kMaxCooldown = 10;

constexpr int kFollowWait = 8;
constexpr int kFollowPause = 6;
constexpr int kLeaderAhead = 5;                     // ticks ahead of the leader
constexpr int kLeaderFirstStep = 6;
constexpr int kLeaderLastStep = 100;
constexpr int kLeaderSide = 40;
constexpr float kOnTheWaySq = 1.3f;                 // 0073FF20
constexpr float kSideOffset = std::bit_cast<float>(0x3FAEB851u);    // 0073FFD8 (1.365)
constexpr float kReachedSq = 0.1f;                  // 0073B850
constexpr float kPassRange = 10.0f;
constexpr float kPassTime = 20.0f;
constexpr int kOrderMove = 1;

constexpr float kSamePointSq = 0.01f;               // 0073C548

// 007C5404 / 007C5408: how close a follower stays and how far its goal may drift before it walks
// again (set at startup).
float FollowNear() { return *reinterpret_cast<const float*>(0x007C5404); }
float FollowFar() { return *reinterpret_cast<const float*>(0x007C5408); }

IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }
int WorldTick() { return *reinterpret_cast<const int*>(0x007AFFC4); }
CAIMap* AIMap() { return WorldServer()->AIMap(); }
Vector3* PositionOf(CUnit* unit) { return &unit->position; }
UnitController* ControllerOf(CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }

// The CPtrList the sector map fills (vtable 0073D454, blocks of 10).
void InitUnitList(mfc::CTypedPtrList<CEngineObject>& list) {
    list = {};
    list.vtable = reinterpret_cast<void*>(0x0073D454);
    list.blockSize = 10;
}
} // namespace

bool UnitController::GetMovePoint(Vector3* out) {
    return reinterpret_cast<CUnitServer*>(this)->GetMovePoint(&out->x) != 0;
}

// 005D5B40
void AIBrain::PlanLooks(AIMoveOrder* order, const Vector3* direction, float wander, bool far) {
    AILookArray& planned = order->looks;
    if (planned.data) {
        gamecrt::operator_delete(planned.data);
        planned.data = nullptr;
    }
    planned.maxSize = 0;
    planned.size = 0;

    IsaacRandom* random = GameRandom();
    int count = static_cast<int>(random->Next() & 3) + 1;
    Vector3 eye = order->point;
    eye.z = StoreFloat(AIMap()->TopHeightAt(&eye) + kEyeHeight);
    if (!(wander >= 0.0f)) wander = kDefaultWander;
    else wander = StoreFloat(wander * kWanderScale);
    if (far) {
        if (owner->busy || std::strncmp(owner->unit->prototypeName, kHumanPrefix, 4) == 0) {
            far = false;
        } else {
            wander = StoreFloat(static_cast<float>(count) * wander);
            count = 1;
        }
    }

    float minWait = StoreFloat(wander * kMinWaitFraction);
    for (; count > 0; --count) {
        int tries = 5;
        float dx, dy, wait;
        for (;;) {
            float angle = StoreFloat(static_cast<float>(x87::FromUnsigned(random->Next()) * kAngleScale));
            double roll = x87::FromUnsigned(random->Next());
            wait = StoreFloat(static_cast<float>((wander - minWait) * roll * kUnitScale + minWait));
            float sine = StoreFloat(static_cast<float>(std::sin(angle)));
            dx = StoreFloat(static_cast<float>(std::cos(angle) + direction->x));
            dy = StoreFloat(sine + direction->y);
            if (tries-- < 0) break;
            Vector3 reach{StoreFloat(dx + dx), StoreFloat(dy + dy), StoreFloat(direction->z + direction->z)};
            Vector3 to{StoreFloat(reach.x + eye.x), StoreFloat(reach.y + eye.y), StoreFloat(reach.z + eye.z)};
            float sight = AIMap()->LineOfSight(&eye, &to);
            if (sight == kOne || x87::IsNaN(sight)) break;
        }
        AILook look{};
        look.angle = StoreFloat(static_cast<float>(std::atan2(dy, dx)));
        look.time = 0.0f;
        look.wait = wait;
        look.stopFirst = far;
        ScriptLooksSetAtGrow(reinterpret_cast<mfc::CArray<AILook>*>(&planned), nullptr, planned.size, &look);
    }
}
EI_REPLACE_METHOD(0x005D5B40, AIBrain::PlanLooks);

// 005D7F80
bool AIBrain::FindStandPoint(const Vector3* point, float radius, Vector3* out) {
    if (standCooldown > 0) {
        --standCooldown;
        return false;
    }
    IsaacRandom* random = GameRandom();
    float low = -radius;
    float dx = StoreFloat(static_cast<float>((radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low));
    double dy = (radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low;
    float radiusSq = StoreFloat(radius * radius);
    for (int tries = 9;;) {
        if (!(dy * dy + dx * dx > radiusSq)) break;
        dx = StoreFloat(static_cast<float>((radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low));
        dy = (radius - low) * x87::FromUnsigned(random->Next()) * kUnitScale + low;
        if (--tries < 0) break;
    }
    out->x = StoreFloat(dx + point->x);
    out->y = StoreFloat(static_cast<float>(dy + point->y));
    out->z = 0.0f;
    CAIMap* map = AIMap();
    if (map->SlopeAt(out) > kMaxSlope) return false;

    // No unit of an allied player may stand near it.
    mfc::CTypedPtrList<CEngineObject> units;
    InitUnitList(units);
    CUnit* self = owner->unit;
    SectorMapFindUnitsInRadius(WorldServer()->Sectors(), nullptr, out, &units, self->standingHeight * kAllySpace);
    for (auto* node = units.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (GameDiplomacy()->allied_[other->side] & (1u << (self->side & 31))) {
            units.RemoveAll();
            return false;
        }
    }

    bool result = true;
    {
        ScopedPath local;
        double ux = self->position.x - out->x;
        double uy = self->position.y - out->y;
        double uz = self->position.z - out->z;
        float distance = StoreFloat(static_cast<float>(std::sqrt(ux * ux + uy * uy + uz * uz)));
        float range = StoreFloat(distance * kRangePerDistance + kRangeBase);
        bool found = map->FindPath(self, out, &local.path, range, 0.0f);
        // A path much longer than the way there (smoothing) counts as none.
        if (!found || !(kOne / (kOne * kHalf) * distance * kDetourFactor >= static_cast<double>(local.path.smoothing))) {
            result = false;
            long cooldown = std::lrint(StoreFloat(distance * kCooldownPerDistance));
            standCooldown = static_cast<int>(std::min<long>(cooldown, kMaxCooldown));
        }
    }
    units.RemoveAll();
    return result;
}
EI_REPLACE_METHOD(0x005D7F80, AIBrain::FindStandPoint);

// 005D6F90
void AIBrain::UpdateFollow() {
    if (!target || ObjectSlots(target)->IsDead()) {
        reinterpret_cast<AIRef*>(&target)->Assign(nullptr);
        action = kActionNone;
        return;
    }
    if (followTimer++ < kFollowWait) return;

    float farSq = StoreFloat(FollowFar() * FollowFar());
    CUnit* self = owner->unit;
    float nearSq = StoreFloat(FollowNear() * FollowNear());
    auto* leader = static_cast<CUnit*>(target);
    Vector3 toLeader{StoreFloat(leader->position.x - self->position.x), StoreFloat(leader->position.y - self->position.y),
                     StoreFloat(leader->position.z - self->position.z)};
    int now = WorldTick();
    float distance = StoreFloat(static_cast<float>(
        std::sqrt(static_cast<double>(toLeader.z) * toLeader.z + toLeader.y * toLeader.y + toLeader.x * toLeader.x)));
    UnitController* leaderController = ControllerOf(leader);
    float walkTime = StoreFloat(distance * kRangePerDistance + kRangeBase);

    if (leaderController->command != kOrderMove && leaderController->nextCommand != kOrderMove) {
        // The leader stands: stop near it, else walk to it when the current goal is too far off.
        if (!(static_cast<double>(distance) * distance >= nearSq)) {
            owner->Idle();
            return;
        }
        Vector3 goal;
        if (!owner->GetMovePoint(&goal)) goal = *PositionOf(self);
        double gx = goal.x - leader->position.x;
        double gy = goal.y - leader->position.y;
        if (gx * gx + gy * gy > farSq) {
            owner->MoveTo(PositionOf(leader), 0, 0, walkTime);
            followTimer = 0;
        }
        return;
    }

    // The leader walks: where it will be in a moment.
    Vector3 goal = *PositionOf(self);
    owner->GetMovePoint(&goal);
    Vector3 soon;
    ObjectSlots(leader)->PositionAt(&soon, now + kLeaderAhead);
    double sx = self->position.x - soon.x;
    double sy = self->position.y - soon.y;
    double soonSq = sx * sx + sy * sy;
    if (soonSq >= static_cast<double>(distance) * distance || soonSq >= nearSq) {
        // Not coming closer: walk to that point unless already going near it.
        double fx = soon.x - goal.x;
        double fy = soon.y - goal.y;
        if (fy * fy + fx * fx > farSq) {
            owner->MoveTo(&soon, 0, 0, walkTime);
            followTimer = kFollowPause;
        }
        return;
    }

    // The leader comes this way: find the moment it passes closest; when it passes within reach,
    // step aside of its way.
    float best = StoreFloat(static_cast<float>(soonSq));
    for (int ahead = kLeaderFirstStep;; ) {
        Vector3 later;
        ObjectSlots(leader)->PositionAt(&later, now + ahead);
        double lx = self->position.x - later.x;
        double ly = self->position.y - later.y;
        double laterSq = lx * lx + ly * ly;
        if (laterSq > best) return;
        if (!(laterSq >= kOnTheWaySq)) break;
        best = StoreFloat(static_cast<float>(laterSq));
        if (++ahead >= kLeaderLastStep) return;
    }

    Vector3 passing;
    ObjectSlots(leader)->PositionAt(&passing, now + kLeaderSide);
    double along = passing.y - leader->position.y;
    float across = StoreFloat(static_cast<float>(passing.x - leader->position.x));
    Vector3 side{StoreFloat(static_cast<float>(-along)), across, 0.0f};
    double lengthSq = static_cast<double>(side.y) * side.y + along * along;
    if (!(lengthSq > 0.0)) return;
    double scale = kSideOffset / std::sqrt(lengthSq);
    side.x = StoreFloat(static_cast<float>(scale * -along));
    side.y = StoreFloat(static_cast<float>(scale * side.y));
    side.z = StoreFloat(static_cast<float>(side.z * scale));
    double ux = self->position.x - leader->position.x;
    double uy = self->position.y - leader->position.y;
    double uz = self->position.z - leader->position.z;
    if (!(side.z * uz + uy * side.y + ux * side.x >= 0.0)) {
        side.x = -side.x;
        side.y = -side.y;
        side.z = -side.z;
    }

    ScopedPath local;
    Vector3 aside{StoreFloat(passing.x + side.x), StoreFloat(passing.y + side.y), StoreFloat(side.z + passing.z)};
    AIMap()->FindPath(self, &aside, &local.path, kPassRange, 0.0f);
    double half = kOne * kHalf;
    float targetY = StoreFloat(static_cast<float>(half * local.path.targetY));
    float offY = StoreFloat(aside.y - targetY);
    double offX = aside.x - half * local.path.targetX;
    if (!(offX * offX + static_cast<double>(offY) * offY >= kReachedSq)) {
        owner->MoveTo(&aside, 0, 0, kPassRange);
        followTimer = kFollowPause;
    } else {
        double px = goal.x - passing.x;
        double py = goal.y - passing.y;
        if (px * px + py * py > kHalf) {
            owner->MoveTo(&passing, 0, 0, kPassTime);
            followTimer = kFollowPause;
        }
    }
}
EI_REPLACE_METHOD(0x005D6F90, AIBrain::UpdateFollow);

// 005D37E0
bool PlayerMode::PathClear(AIBrain* brain, const Vector3* from, const Vector3* to) {
    double fy = from->y - to->y;
    double fx = from->x - to->x;
    if (!(fx * fx + fy * fy >= kSamePointSq)) return false;
    ScopedPath local;
    double dx = from->x - to->x;
    double dy = from->y - to->y;
    double dz = from->z - to->z;
    CUnit* unit = brain->owner->unit;
    Vector3 saved = *PositionOf(unit);
    float range = StoreFloat(static_cast<float>(2.0 * std::sqrt(dz * dz + dy * dy + dx * dx)));
    SectorMapMove(WorldServer()->Sectors(), nullptr, unit, from);
    bool found = AIMap()->FindPath(unit, to, &local.path, range, 0.0f);
    SectorMapMove(WorldServer()->Sectors(), nullptr, unit, &saved);
    return found;
}
EI_REPLACE_METHOD(0x005D37E0, PlayerMode::PathClear);
