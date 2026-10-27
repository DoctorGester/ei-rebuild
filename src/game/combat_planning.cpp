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

#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kOrderAttack = 3;
constexpr int kOrderCastAt = 4;
constexpr int kOrderCast = 5;
constexpr int kOrderSkill = 6;
constexpr int kMotionPath = 2;                    // MotionOrder kind: follow the steps
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kFarBits = 0x49742400u;   // 1e6
constexpr float kFar = std::bit_cast<float>(kFarBits);
constexpr std::uint32_t kNoZBits = 0xBF800000u;   // -1.0: an order point without height
constexpr int kNoSpot = 0x7FFFFFFF;
constexpr int kShoutTargetLost = 0x0B;
constexpr int kShoutCannotDo = 0x0E;

CMapObjectVirtuals* Slots(CEngineObject* object) { return ObjectSlots(object); }

// --- Helpers --------------------------------------------------------------------------------------

void __fastcall PointArrayConstruct(void* a0) { mfc::impl::ConstructHeader<mfc::impl::PointArrayClass>(a0); }
inline void __fastcall PlexFreeDataChain(void* p) { mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(p)); }

// The server's AI map's refreshUnits, cleared around path searches, and its sector map.
std::uint8_t& PathFlag() { return TheServerWorld()->AIMap()->refreshUnits; }
CSectorMap* ServerSectors() { return TheServerWorld()->Sectors(); }
CAIMap* MapOf(CWorldServer* world) { return world->AIMap(); }
float HalfCell() { return 1.0f /*0073EEE4*/ * 0.5f /*0073B86C*/; }

// An AIPath on the stack: its lists and point array constructed, the order cleared.
struct LocalPath {
    lib::InPlace<AIPath> path;
    AIPath* Path() { return path.Get(); }
    const AIPath* Path() const { return &path.object; }
    LocalPath() {
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&Path()->steps, 0x80);
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&Path()->cells, 0x80);
        PointArrayConstruct(&Path()->excludedBlocks);
        Path()->Clear();
    }
    ~LocalPath() {
        mfc::impl::DestructHeader<mfc::impl::PointArrayClass>(&Path()->excludedBlocks);
        mfc::impl::DestructList<mfc::impl::CellListClass>(Path()->cells);
        mfc::impl::DestructList<mfc::impl::CellListClass>(Path()->steps);
    }
    LocalPath(const LocalPath&) = delete;
    LocalPath& operator=(const LocalPath&) = delete;
    float TargetX() const { return Path()->targetX; }
    float TargetY() const { return Path()->targetY; }
    int Cost() const { return Path()->smoothing; }
};
static_assert(sizeof(AIPath) == 0x88);

// A motion order on the stack (the order and its step list), as AIPath::ToMotionOrder fills it. The order part
// is zeroed (the original leaves it uninitialised until it is filled).
struct LocalMotionOrder : PathOrder {
    LocalMotionOrder() {
        std::memset(&motionOrder, 0, sizeof(motionOrder));
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&steps, 0x80);
    }
    ~LocalMotionOrder() { mfc::impl::DestructList<mfc::impl::CellListClass>(steps); }
    LocalMotionOrder(const LocalMotionOrder&) = delete;
    LocalMotionOrder& operator=(const LocalMotionOrder&) = delete;
};
static_assert(sizeof(LocalMotionOrder) == 0x3C);

// A UnitMotion on the stack, cleared, with the speed scale, no turning limit (1e10) and no segment.
struct LocalUnitMotion {
    UnitMotion motion;
    LocalUnitMotion() {
        mfc::impl::ConstructArrayOf<mfc::impl::MotionPointArrayClass>(motion.points);
        motion.points.vtable = mfc::impl::MotionPointsClass::Vtable();
        mfc::impl::ConstructArrayOf<mfc::impl::MotionWaypointArrayClass>(motion.waypoints);
        mfc::impl::ConstructArrayOf<mfc::impl::ArgFloatArrayClass>(motion.stepSpeeds);
        motion.curveX = {};   // the segment part: no curves, no segment
        motion.curveY = {};
        motion.segment = -1;
        motion.Clear();
        motion.speedScale = 0.5f /*0073EF20*/;
        motion.segment = -1;
        motion.baseTurnRate = std::bit_cast<float>(0x501502F9u);   // 1e10
    }
    ~LocalUnitMotion() {
        mfc::impl::DestructArrayOf<mfc::impl::FloatArrayClass>(motion.stepSpeeds);
        mfc::impl::DestructArrayOf<mfc::impl::MotionWaypointBaseClass>(motion.waypoints);
        mfc::impl::DestructArrayOf<mfc::impl::MotionPointArrayClass>(motion.points);
    }
    LocalUnitMotion(const LocalUnitMotion&) = delete;
    LocalUnitMotion& operator=(const LocalUnitMotion&) = delete;
};

using SpotArray = UseSpotArray;

using UnitPtrList = mfc::CTypedPtrList<CUnit>;

CPlayerServer* PlayerConnection(CPlayer* player) { return player->Server(); }

void PathMessage(CUnit* unit, PathOrder* motion, const float* point, int kind, int flag, float limit) {
    if (unit->player) PlayerConnection(unit->player)->SendPathMessage(unit, motion, point, kind, flag, std::bit_cast<std::uint32_t>(limit));
}

} // namespace

// --- A use order's spot -----------------------------------------------------------------------

bool CUnitServer::FindUseSpot(UnitAction* order, PathOrder* out) {
    CWorldServer* world = ServerWorld();
    order->point.z = std::bit_cast<float>(kNoZBits);
    if (order->kind != kOrderSkill || !order->target || order->source != 0) return true;
    SpotArray spots{mfc::impl::Vector3ArrayClass::Vtable(), nullptr, 0, 0, 0};
    float size = static_cast<float>(Slots(unit)->Radius());
    order->target->UseSpots(&spots, size);
    bool result = true;
    if (spots.size > 0) {
        LocalPath path;
        int best = kNoSpot;
        Vector3 spot;
        Vector3 bestSpot = {0.0f, 0.0f, 0.0f};
        CAIMap* map = MapOf(world);
        for (int i = 0; i < spots.size; ++i) {
            spot = spots.data[i];
            path.Path()->Clear();
            if (!map->FindPath(unit, &spot, path.Path(), kFar, 0.0f)) continue;
            // The path must end within 0.2 of the spot.
            float half = HalfCell();
            float ey = half * path.TargetY() - spot.y;
            float ex = half * path.TargetX() - spot.x;
            float limitSquared = static_cast<float>(0.2f /*0073EF28*/ * 0.2f /*0073EF28*/);   // rounded to 24 bits
            EI_NAN_AUDIT(ex, ey);
            if (LengthSquared2DUnrounded(ex, ey) >= limitSquared) continue;
            if (path.Cost() >= best) continue;
            best = path.Cost();
            std::memcpy(&bestSpot, &spot, sizeof(bestSpot));
            if (out) path.Path()->ToMotionOrder(out);
        }
        if (best == kNoSpot) {
            result = false;
        } else {
            order->flag18 = 1;
            std::memcpy(&order->point, &bestSpot, sizeof(bestSpot));
        }
    }
    spots.vtable = mfc::impl::Vector3ArrayClass::Vtable();
    if (spots.data) gamecrt::operator_delete(spots.data);
    return result;
}
EI_REWRITTEN(0x0054DF50, CUnitServer::FindUseSpot);

// --- The time along a path until an action is possible -----------------------------------------

double CUnitServer::TimeUntilAction(const AIOrder* order, PathOrder* motionOrder) {
    const MotionOrder* pathOrder = OrderOf(motionOrder);
    if (pathOrder->kind != kMotionPath) return 0.0f;
    ObjectWorld(this);
    LocalUnitMotion simulated;
    simulated.motion.Build(motionOrder);
    SightTarget sight;
    bool canSee = FindSightTarget(order, &sight);
    Vector3 previous = {0.0f, 0.0f, -1000.0f};
    Vector3 aim = {0.0f, 0.0f, -1000.0f};
    double distance = ActionDistance(order);
    float reachSq = static_cast<float>(distance * distance);
    if (!order->target) std::memcpy(&aim, &order->point, sizeof(aim));
    else order->target->AimPoint(&aim);
    float t = 0.0f;
    for (;;) {
        float time = static_cast<float>(pathOrder->startTime) + t;
        bool turning;
        Vector3 position;
        Vector3 direction;
        float speed;
        int cellX;
        int cellY;
        simulated.motion.Sample(nullptr, OrderOf(motionOrder), time, &turning,
                                                             &position.x, &direction.x, &speed, &cellX, &cellY);
        if (position.x == previous.x && position.y == previous.y && position.z == previous.z) break;
        std::memcpy(&previous, &position, sizeof(previous));
        float half = HalfCell();
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
            Vector3 eye;
            const float* at = &Slots(sight.unit)->EyePoint(&eye)->x;
            std::memcpy(&sight.point, at, sizeof(sight.point));
            sight.unit = nullptr;
        }
        float sy = sight.point.y - position.y;
        float sx = sight.point.x - position.x;
        EI_NAN_AUDIT(sy, sx, sight.minDistanceSq);
        if (LengthSquared2DUnrounded(sy, sx) < sight.minDistanceSq) break;   // closer
        Vector3 from = {position.x, position.y, position.z};
        from.z = static_cast<float>(unit->GroundHeight(position.x, position.y) + sight.eyeHeight);
        double visible = MapOf(ServerWorld())->LineOfSight(&from, &sight.point);
        EI_NAN_AUDIT(visible);
        if (visible >= 0.1f /*0073EEC0*/) break;
        t += 5.0f;
    }
    return t;
}
EI_REWRITTEN(0x0054E250, CUnitServer::TimeUntilAction);

// --- Planning an order's way ---------------------------------------------------------------------

bool CUnitServer::PlanOrderRoute(UnitAction* order, std::uint32_t shout) {
    CPlayer* player = unit->player;
    if (order->kind != kOrderSkill && !player) return true;
    float best = kFar;
    LocalMotionOrder route;
    Vector3 point;
    if (!order->target) std::memcpy(&point, &order->point, sizeof(point));
    else order->target->AimPoint(&point);
    bool ok;
    if (!FindUseSpot(order, &route)) {
        PathMessage(unit, &route, &point.x, 3, 1, kFar);
        ok = false;
    } else {
        if (!player) return true;
        if (CanReachTarget(order) && CanPlanAction(order)) {
            // The action can be done from here.
            PathMessage(unit, &route, &point.x, 3, 0, kFar);
            if ((shout <= 0x29 || shout == 0x30) && shout != 0xFFFFFFFFu && unit->player)
                PlayerMessage(PlayerConnection(unit->player), unit, static_cast<int>(shout));
            return true;
        }
        int kind = order->kind == kOrderAttack ? 2 : 1;
        if (order->kind == kOrderCastAt || order->kind == kOrderCast) {
            std::uint32_t source = action.source;
            CSpell* spell = nullptr;
            bool skip = false;
            if (source & kQuickItemSource) {
                CItem* carrier = SpellCarrier(source);
                if (!carrier) skip = true;
                else spell = carrier->spell_;
            } else {
                spell = unit->GetSpellObject(source);
            }
            if (!skip && spell && spell->GetPrototype()->castType == 0) kind = 2;
        }
        // Where the unit would stand at the end of its way, and whether it could act from there.
        auto actsFromDestination = [&]() {
            Vector3 destination;
            const Vector3* at = route.Destination(&destination);
            if (!CanReachTarget(order)) return false;
            float x = unit->localPosition_.x, y = unit->localPosition_.y, z = unit->localPosition_.z;
            unit->localPosition_.x = at->x;
            unit->localPosition_.y = at->y;
            unit->localPosition_.z = at->z;
            bool can = CanPlanAction(order);
            unit->localPosition_.x = x;
            unit->localPosition_.y = y;
            unit->localPosition_.z = z;
            return can;
        };
        if (order->flag18 == 0) {
            LocalPath path;
            CMapObject* target = order->target;
            std::uint8_t savedFlag = 0;
            if (target) {
                savedFlag = PathFlag();
                PathFlag() = false;
                ServerSectors()->Remove(target);
            }
            CWorldServer* world = ServerWorld();
            path.Path()->flag6E = unit->player == nullptr;
            std::uint32_t saved43C = unit->altPassCosts;
            unit->altPassCosts = 1;
            bool found = MapOf(world)->FindPath(unit, &point, path.Path(), kFar, 0.0f);
            unit->altPassCosts = saved43C;
            if (!found) path.Path()->StandStill(unit);
            if (target) {
                ServerSectors()->Add(target);
                PathFlag() = savedFlag;
            }
            path.Path()->ToMotionOrder(&route);
            ok = actsFromDestination();
            if (ok) best = static_cast<float>(TimeUntilAction(order, &route));
        } else {
            ok = actsFromDestination();
        }
        PathMessage(unit, &route, &point.x, kind, ok ? 0 : 1, best);
        if (ok) {
            Shout(shout);
            return true;
        }
    }
    Shout(order->kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
    return ok;
}
EI_REWRITTEN(0x0054E600, CUnitServer::PlanOrderRoute);

// --- A party unit's move -----------------------------------------------------------------------

void CUnitServer::PlanMove(const Vector3* point, int moving, int run, float range) {
    CWorldServer* world = ServerWorld();
    LocalPath path;
    UnitPtrList lifted{mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    LiftNearby(&lifted, point, static_cast<char>(moving));
    int mode = unit->moveMode;
    CSectorMap* sectors = world->Sectors();
    CAIMap* map = MapOf(world);
    bool running = static_cast<std::uint8_t>(run) != 0;
    if (running && mode != 2 && mode != 3) {
        // A runner plans as walking (mode 2) when it may leave its cell that way.
        sectors->Remove(unit);
        unit->moveMode = 2;
        unit->UpdateSizeClass();
        if (!map->CanLeaveCell(unit)) unit->moveMode = mode;
        sectors->Add(unit);
    }
    std::uint32_t flag = map->CellFlag80At(&unit->localPosition_);
    CWorldServer* world2 = ServerWorld();
    path.Path()->flag6E = unit->player == nullptr;
    std::uint32_t saved43C = unit->altPassCosts;
    unit->altPassCosts = flag & 0xFF;
    bool found = MapOf(world2)->FindPath(unit, point, path.Path(), range, 0.0f);
    unit->altPassCosts = saved43C;
    if (running) {
        sectors->Remove(unit);
        unit->moveMode = mode;
        unit->UpdateSizeClass();
        sectors->Add(unit);
    }
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        sectors->Add(current->data);
    }
    ReportPath(path.Path(), found ? 1 : 0);
    lifted.count = 0;
    lifted.free = nullptr;
    lifted.tail = nullptr;
    lifted.head = nullptr;
    PlexFreeDataChain(lifted.blocks);
    lifted.blocks = nullptr;
}
EI_REWRITTEN(0x00554830, CUnitServer::PlanMove);
