// Combat, eighth part: planning an order's way (PlanOrder), the spot a use order goes to, the time
// along a path until an action can be carried out, and a party unit's move. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Order lifecycle").
// These build paths on the stack: an AIPath and the motion order a path turns into, for the player's path
// messages and the simulation of a unit's motion.
#include "ai_map.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "ai_motion.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "player_messages.h"
#include "sector_map.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "lib/bytes.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>

namespace {

constexpr int kMotionPath = 2;                    // MotionOrder kind: follow the steps
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr float kFarBits = 1000000.0f;   // 1e6
constexpr float kFar = kFarBits;
constexpr float kNoZBits = -1.0f;   // -1.0: an order point without height
constexpr int kNoSpot = 0x7FFFFFFF;

// --- Helpers --------------------------------------------------------------------------------------

// The server's AI map's refreshUnits, cleared around path searches, and its sector map.
AIMap* MapOf(WorldServer* world) { return &world->aiMap; }
constexpr float kHalfCell = 0.5f;

// A UnitMotion on the stack, cleared, with the speed scale, no turning limit (1e10) and no segment.
struct LocalUnitMotion {
    UnitMotion motion;
    LocalUnitMotion() {
        motion.curveX = {};   // the segment part: no curves, no segment
        motion.curveY = {};
        motion.segment = -1;
        motion.Clear();
        motion.speedScale = 0.5f;
        motion.segment = -1;
        static_assert(std::bit_cast<std::uint32_t>(1e10f) == 0x501502F9u);
        motion.baseTurnRate = 1e10f;   // 1e10
    }
    LocalUnitMotion(const LocalUnitMotion&) = delete;
    LocalUnitMotion& operator=(const LocalUnitMotion&) = delete;
};


void PathMessage(Unit* unit, PathOrder* motion, const float* point, int kind, int flag, float limit) {
    if (unit->player) unit->player->Server()->SendPathMessage(unit, motion, point, kind, flag, std::bit_cast<std::uint32_t>(limit));
}

} // namespace

// --- A use order's spot -----------------------------------------------------------------------

bool UnitServer::FindUseSpot(UnitAction* order, PathOrder* out) {
    WorldServer* world = ServerWorld();
    order->point.z = kNoZBits;
    if (order->kind != UnitOrderKind::Use || !order->target || order->source != 0) return true;
    float size = static_cast<float>(unit->Radius());
    std::vector<Vector3> spots = order->target->UseSpots(size);
    bool result = true;
    if (!spots.empty()) {
        AIPath path;
        int best = kNoSpot;
        Vector3 spot;
        Vector3 bestSpot = {0.0f, 0.0f, 0.0f};
        AIMap* map = MapOf(world);
        for (int i = 0; i < std::ssize(spots); ++i) {
            spot = spots[i];
            path.Clear();
            if (!map->FindPath(unit, spot, &path, kFar, 0.0f)) continue;
            // The path must end within 0.2 of the spot.
            float half = kHalfCell;
            float ey = half * path.targetY - spot.y;
            float ex = half * path.targetX - spot.x;
            float limitSquared = static_cast<float>(0.2f * 0.2f);   // rounded to 24 bits
            EI_NAN_AUDIT(ex, ey);
            if (LengthSquared2DUnrounded(ex, ey) >= limitSquared) continue;
            if (path.smoothing >= best) continue;
            best = path.smoothing;
            bestSpot = spot;
            if (out) path.ToMotionOrder(out);
        }
        if (best == kNoSpot) {
            result = false;
        } else {
            order->pointIsUseSpot = true;
            order->point = bestSpot;
        }
    }
    return result;
}

// --- The time along a path until an action is possible -----------------------------------------

float UnitServer::TimeUntilAction(const AIOrder* order, PathOrder* motionOrder) {
    const MotionOrder* pathOrder = OrderOf(motionOrder);
    if (pathOrder->kind != kMotionPath) return 0.0f;
    LocalUnitMotion simulated;
    simulated.motion.Build(motionOrder);
    SightTarget sight;
    bool canSee = FindSightTarget(order, &sight);
    Vector3 previous = {0.0f, 0.0f, -1000.0f};
    Vector3 aim = {0.0f, 0.0f, -1000.0f};
    float distance = ActionDistance(order);
    float reachSq = distance * distance;
    if (!order->target) aim = order->point;
    else aim = order->target->AimPoint();
    float t = 0.0f;
    for (;;) {
        float time = pc24::Add(static_cast<double>(pathOrder->startTime), t);   // the int exact, one rounding
        MotionSample sample = simulated.motion.Sample(nullptr, OrderOf(motionOrder), time);
        Vector3 position = {sample.x, sample.y, 0.0f};
        if (position.x == previous.x && position.y == previous.y && position.z == previous.z) break;
        previous = position;
        float half = kHalfCell;
        position.x = half * position.x;
        position.y = half * position.y;
        float dx = position.x - aim.x;
        float dy = position.y - aim.y;
        if (LengthSquared2DUnrounded(dy, dx) > reachSq) {
            t += 5.0f;
            continue;
        }
        // In reach: done unless a line of sight is needed and missing from here.
        if (!canSee) break;
        if (sight.unit) {
            sight.point = sight.unit->EyePoint();
            sight.unit = nullptr;
        }
        float sy = sight.point.y - position.y;
        float sx = sight.point.x - position.x;
        EI_NAN_AUDIT(sy, sx, sight.minDistanceSq);
        if (LengthSquared2DUnrounded(sy, sx) < sight.minDistanceSq) break;   // closer
        Vector3 from = {position.x, position.y, position.z};
        from.z = pc24::Add(unit->GroundHeight(position.x, position.y), sight.eyeHeight);   // a double plus a float
        float visible = MapOf(ServerWorld())->LineOfSight(from, sight.point);
        EI_NAN_AUDIT(visible);
        if (visible >= 0.1f) break;
        t += 5.0f;
    }
    return t;
}

// --- Planning an order's way ---------------------------------------------------------------------

bool UnitServer::PlanOrderRoute(UnitAction* order, AckKind shout) {
    Player* player = unit->player;
    if (order->kind != UnitOrderKind::Use && !player) return true;
    float best = kFar;
    LocalMotionOrder route;
    Vector3 point;
    if (!order->target) point = order->point;
    else point = order->target->AimPoint();
    bool ok;
    if (!FindUseSpot(order, &route)) {
        PathMessage(unit, &route, &point.x, 3, 1, kFar);
        ok = false;
    } else {
        if (!player) return true;
        if (CanReachTarget(order) && CanPlanAction(order)) {
            // The action can be done from here.
            PathMessage(unit, &route, &point.x, 3, 0, kFar);
            if (IsShoutable(shout) && unit->player) PlayerMessage(unit->player->Server(), unit, shout);
            return true;
        }
        int kind = order->kind == UnitOrderKind::Attack ? 2 : 1;
        if (order->kind == UnitOrderKind::CastAt || order->kind == UnitOrderKind::CastOn) {
            std::uint32_t source = action.source;
            Spell* spell = nullptr;
            bool skip = false;
            if (source & kQuickItemSource) {
                Item* carrier = SpellCarrier(source);
                if (!carrier) skip = true;
                else spell = carrier->spell;
            } else {
                spell = unit->GetSpellObject(source);
            }
            if (!skip && spell && spell->GetPrototype()->castType == 0) kind = 2;
        }
        // Where the unit would stand at the end of its way, and whether it could act from there.
        auto actsFromDestination = [&]() {
            Vector3 at = route.Destination();
            if (!CanReachTarget(order)) return false;
            float x = unit->localPosition.x, y = unit->localPosition.y, z = unit->localPosition.z;
            unit->localPosition.x = at.x;
            unit->localPosition.y = at.y;
            unit->localPosition.z = at.z;
            bool can = CanPlanAction(order);
            unit->localPosition.x = x;
            unit->localPosition.y = y;
            unit->localPosition.z = z;
            return can;
        };
        if (!order->pointIsUseSpot) {
            AIPath path;
            MapObject* target = order->target;
            std::uint8_t savedFlag = 0;
            if (target) {
                savedFlag = g_serverWorld.aiMap.refreshUnits;
                g_serverWorld.aiMap.refreshUnits = false;
                g_serverWorld.aiMap.sectorMap.Remove(target);
            }
            WorldServer* world = ServerWorld();
            path.mayExcludeRegions = unit->player == nullptr;
            std::uint32_t savedAltPassCosts = unit->altPassCosts;
            unit->altPassCosts = 1;
            bool found = MapOf(world)->FindPath(unit, point, &path, kFar, 0.0f);
            unit->altPassCosts = savedAltPassCosts;
            if (!found) path.StandStill(unit);
            if (target) {
                g_serverWorld.aiMap.sectorMap.Add(target);
                g_serverWorld.aiMap.refreshUnits = savedFlag;
            }
            path.ToMotionOrder(&route);
            ok = actsFromDestination();
            if (ok) best = TimeUntilAction(order, &route);
        } else {
            ok = actsFromDestination();
        }
        PathMessage(unit, &route, &point.x, kind, ok ? 0 : 1, best);
        if (ok) {
            Shout(shout);
            return true;
        }
    }
    Shout(order->kind == UnitOrderKind::Attack ? AckKind::NoWayToAttack : AckKind::NoPath);
    return ok;
}

// --- A party unit's move -----------------------------------------------------------------------

void UnitServer::PlanMove(const Vector3& point, int moving, int run, float range) {
    WorldServer* world = ServerWorld();
    AIPath path;
    std::vector<Unit*> lifted = LiftNearby(point, static_cast<char>(moving));
    int mode = unit->moveMode;
    SectorMap* sectors = &world->aiMap.sectorMap;
    AIMap* map = MapOf(world);
    bool running = static_cast<std::uint8_t>(run) != 0;
    if (running && mode != 2 && mode != 3) {
        // A runner plans as walking (mode 2) when it may leave its cell that way.
        sectors->Remove(unit);
        unit->moveMode = 2;
        unit->UpdateSizeClass();
        if (!map->CanLeaveCell(unit)) unit->moveMode = mode;
        sectors->Add(unit);
    }
    std::uint32_t flag = map->InWaterAt(unit->localPosition);
    WorldServer* world2 = ServerWorld();
    path.mayExcludeRegions = unit->player == nullptr;
    std::uint32_t savedAltPassCosts = unit->altPassCosts;
    unit->altPassCosts = flag & 0xFF;
    bool found = MapOf(world2)->FindPath(unit, point, &path, range, 0.0f);
    unit->altPassCosts = savedAltPassCosts;
    if (running) {
        sectors->Remove(unit);
        unit->moveMode = mode;
        unit->UpdateSizeClass();
        sectors->Add(unit);
    }
    for (Unit* other : lifted) sectors->Add(other);
    ReportPath(&path, found ? 1 : 0);
}
