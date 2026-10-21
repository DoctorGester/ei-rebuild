// Combat, eighth part: planning an order's way (PlanOrder), the spot a use order goes to, the time
// along a path until an action can be carried out, and a party unit's move. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Order lifecycle").
//
// These build paths on the stack: an AIPath (0x88 bytes: the motion order, the step and cell lists
// at +0x20 / +0x3C, a point array at +0x70) and the motion order a path turns into (0x3C bytes:
// the order and a step list) for the player's path messages and the simulation of a unit's motion.
#include "ai_map.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "ai_motion.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "player_messages.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"

#include "ei/hook.h"
#include "originals_effects.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"
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

// The map objects' virtual slots (map_object.h): 53 AimPoint, 48 EyePoint, 44 UseSpots, 60 Radius.
CMapObjectVirtuals* Slots(CEngineObject* object) { return ObjectSlots(object); }
void AimPointInto(CEngineObject* object, Vector3* out) { Slots(object)->AimPoint(out); }

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

inline double __fastcall UnitGroundHeight(CUnit* a0, void*, float a2, float a3) { return static_cast<CUnit*>((void*)a0)->GroundHeight(a2, a3); }
// The AI map (world + 0x170): 005BB350 FindPath, 005BA830 CanLeaveCell, 005BBC70 the cell flag
// 0x80 under a point, 005BC3E0 LineOfSight (ST0).
// The sector map (world + 0x294 = 007B01F4): 005C1010 Remove, 005C0DF0 Add.
// AIPath (ai_motion.cpp / ai_path_stand.cpp): 005B23A0 Clear, 005B2190 stand still where the unit
// is, 005B22C0 the path as a motion order, 005C7D00 Destination (of a motion order).
// UnitMotion (ai_motion.cpp): 005C67D0 Build, 005C7430 Sample (005C6270 Clear is in originals_ai.h).
// The controller's functions rewritten elsewhere: 0054C800
// CanReachTarget, 0054C6B0 CanPlanAction, 0054BCB0 FindSightTarget (combat_sight.cpp), 0054C070
// ActionDistance (ST0), 0054B7C0 SpellCarrier (combat.cpp), 00552780 Shout (unit_create.cpp),
// 005468D0 LiftNearby, 0054EB30 ReportPath (combat_motion.cpp), and this file's functions.
// 0066D490: the player's message about a path (combat_motion.cpp).
// Containers: 00545750 a path cell list's constructor (blocks of 0x80), 00539890 its destructor,
// 00557230 / 00557250 a point array's, the UnitMotion members' 00556D70 / 00556810 / 004D5990 /
// 0054E5D0 and 004CB050; MFC 006FE90F CPlex::FreeDataChain.
void __fastcall PointArrayConstruct(void* a0) { mfc::impl::ConstructHeader<mfc::impl::PointArrayClass>(a0); }
inline void __fastcall PlexFreeDataChain(void* p) { PlexFreeDataChainOriginal(p); }

// The server's AI map's refreshUnits (007B00D2), cleared around path searches, and its sector
// map (007B01F4).
bool& PathFlag() { return TheServerWorld()->AIMap()->refreshUnits; }
CSectorMap* ServerSectors() { return TheServerWorld()->Sectors(); }
CAIMap* MapOf(void* world) { return static_cast<CWorldServer*>(world)->AIMap(); }
float HalfCell() { return 1.0f /*0073EEE4*/ * 0.5f /*0073B86C*/; }

// An AIPath on the stack: its lists and point array constructed, the order cleared.
struct LocalPath {
    alignas(4) std::uint8_t bytes[sizeof(AIPath)];
    AIPath* Path() { return reinterpret_cast<AIPath*>(bytes); }
    const AIPath* Path() const { return reinterpret_cast<const AIPath*>(bytes); }
    LocalPath() {
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&Path()->steps, 0x80);
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&Path()->cells, 0x80);
        PointArrayConstruct(&Path()->excludedBlocks);
        reinterpret_cast<AIPath*>(bytes)->Clear();
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

// A motion order on the stack (the order and its step list), as 005B22C0 fills it. The order
// part is zeroed (the original leaves it uninitialised until it is filled).
struct LocalMotionOrder {
    MotionOrder order;
    PathCellList steps;
    LocalMotionOrder() {
        std::memset(&order, 0, sizeof(order));
        mfc::impl::ConstructListHeader<mfc::impl::CellListClass>(&steps, 0x80);
    }
    ~LocalMotionOrder() { mfc::impl::DestructList<mfc::impl::CellListClass>(steps); }
    LocalMotionOrder(const LocalMotionOrder&) = delete;
    LocalMotionOrder& operator=(const LocalMotionOrder&) = delete;
    void* bytes() { return this; }
};
static_assert(sizeof(LocalMotionOrder) == 0x3C);

// A UnitMotion on the stack, cleared, with the speed scale 0073EF20, no turning limit (1e10) and
// no segment.
struct LocalUnitMotion {
    UnitMotion motion;
    LocalUnitMotion() {
        mfc::impl::ConstructArrayOf<mfc::impl::MotionPointArrayClass>(motion.points);
        motion.points.vtable = ei::GamePointer<void>(0x0073EB9C);
        mfc::impl::ConstructArrayOf<mfc::impl::MotionWaypointArrayClass>(motion.waypoints);
        mfc::impl::ConstructArrayOf<mfc::impl::ArgFloatArrayClass>(motion.stepSpeeds);
        motion.curveX = {};   // the segment part (0054E5D0): no curves, no segment
        motion.curveY = {};
        motion.segment = -1;
        static_cast<UnitMotion*>(&motion)->Clear();
        motion.speedScale = 0.5f /*0073EF20*/;
        motion.segment = -1;
        motion.baseTurnRate = std::bit_cast<float>(0x501502F9u);   // 1e10
    }
    ~LocalUnitMotion() {
        mfc::impl::DestructArrayOf<mfc::impl::FloatArrayClass>(motion.stepSpeeds);
        motion.waypoints.vtable = ei::GamePointer<void>(0x0073EBB4);
        if (motion.waypoints.data) gamecrt::operator_delete(motion.waypoints.data);
        motion.points.vtable = ei::GamePointer<void>(0x0073EBA8);
        if (motion.points.data) gamecrt::operator_delete(motion.points.data);
    }
    LocalUnitMotion(const LocalUnitMotion&) = delete;
    LocalUnitMotion& operator=(const LocalUnitMotion&) = delete;
};

// A CArray<Vector3> (vtable 0073C120) of the spots a use order may go to.
using SpotArray = mfc::CArray<float[3]>;

// A CPtrList on the stack (vtable 0073D454, blocks of 10).
using UnitPtrList = mfc::CTypedPtrList<CEngineObject>;

// The messages of a unit's player (CPlayer::connection_).
void* PlayerConnection(char* player) { return reinterpret_cast<CPlayer*>(player)->connection_; }

void PathMessage(CUnit* unit, const void* motion, const float* point, int kind, int flag, float limit) {
    if (unit->player) reinterpret_cast<CPlayerServer*>(PlayerConnection(unit->player))->SendPathMessage(unit, const_cast<void*>(motion), point, kind, flag, std::bit_cast<std::uint32_t>(limit));
}

} // namespace

// --- A use order's spot -----------------------------------------------------------------------

bool CUnitServer::FindUseSpot(UnitAction* order, void* out) {
    void* world = ObjectWorld(this);
    order->point.z = std::bit_cast<float>(kNoZBits);
    if (order->kind != kOrderSkill || !order->target || order->source != 0) return true;
    SpotArray spots{ei::GamePointer<void>(0x0073C120), nullptr, 0, 0, 0};
    float size = static_cast<float>(Slots(unit)->Radius());
    Slots(order->target)->UseSpots(&spots, size);
    bool result = true;
    if (spots.size > 0) {
        LocalPath path;
        int best = kNoSpot;
        Vector3 spot;
        Vector3 bestSpot = {0.0f, 0.0f, 0.0f};
        CAIMap* map = MapOf(world);
        for (int i = 0; i < spots.size; ++i) {
            std::memcpy(&spot, spots.data[i], sizeof(spot));
            reinterpret_cast<AIPath*>(path.bytes)->Clear();
            if (!MapFindPath(map, nullptr, unit, &spot, path.bytes, kFar, 0.0f)) continue;
            // The path must end within 0073EF28 of the spot.
            float half = HalfCell();
            float ey = half * path.TargetY() - spot.y;
            float ex = half * path.TargetX() - spot.x;
            float limitSquared = static_cast<float>(0.2f /*0073EF28*/ * 0.2f /*0073EF28*/);   // rounded to 24 bits, as the FMUL
            EI_NAN_AUDIT(ex, ey);
            if (LengthSquared2DUnrounded(ex, ey) >= limitSquared) continue;
            if (path.Cost() >= best) continue;
            best = path.Cost();
            std::memcpy(&bestSpot, &spot, sizeof(bestSpot));
            if (out) path.Path()->ToMotionOrder(static_cast<AIPath*>(out));
        }
        if (best == kNoSpot) {
            result = false;
        } else {
            order->flag18 = 1;
            std::memcpy(&order->point, &bestSpot, sizeof(bestSpot));
        }
    }
    spots.vtable = ei::GamePointer<void>(0x0073C120);
    if (spots.data) gamecrt::operator_delete(spots.data);
    return result;
}
EI_REPLACE_METHOD(0x0054DF50, CUnitServer::FindUseSpot);

// --- The time along a path until an action is possible -----------------------------------------

double CUnitServer::TimeUntilAction(const UnitAction* order, void* motionOrder) {
    const auto* pathOrder = static_cast<const MotionOrder*>(motionOrder);
    if (pathOrder->kind != kMotionPath) return 0.0f;
    ObjectWorld(this);
    LocalUnitMotion simulated;
    static_cast<UnitMotion*>(&simulated.motion)->Build(static_cast<AIPath*>(motionOrder));
    SightTarget sight;
    bool canSee = FindSightTarget(order, &sight);
    Vector3 previous = {0.0f, 0.0f, -1000.0f};
    Vector3 aim = {0.0f, 0.0f, -1000.0f};
    double distance = ActionDistance(order);
    float reachSq = static_cast<float>(distance * distance);
    if (!order->target) std::memcpy(&aim, &order->point, sizeof(aim));
    else AimPointInto(order->target, &aim);
    float t = 0.0f;
    for (;;) {
        float time = static_cast<float>(pathOrder->startTime) + t;
        bool turning;
        Vector3 position;
        Vector3 direction;
        float speed;
        int cellX;
        int cellY;
        static_cast<UnitMotion*>(&simulated.motion)->Sample(nullptr, OrderOf(static_cast<AIPath*>(motionOrder)), time, &turning,
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
        from.z = static_cast<float>(UnitGroundHeight(unit, nullptr, position.x, position.y) + sight.eyeHeight);
        double visible = MapOf(ManagerContextOf(this))->LineOfSight(&from, &sight.point);
        EI_NAN_AUDIT(visible);
        if (visible >= 0.1f /*0073EEC0*/) break;
        t += 5.0f;
    }
    return t;
}
EI_REPLACE_METHOD(0x0054E250, CUnitServer::TimeUntilAction);

// --- Planning an order's way ---------------------------------------------------------------------

bool CUnitServer::PlanOrderRoute(UnitAction* order, std::uint32_t shout) {
    char* player = unit->player;
    if (order->kind != kOrderSkill && !player) return true;
    float best = kFar;
    LocalMotionOrder route;
    Vector3 point;
    if (!order->target) std::memcpy(&point, &order->point, sizeof(point));
    else AimPointInto(order->target, &point);
    bool ok;
    if (!FindUseSpot(order, route.bytes())) {
        PathMessage(unit, route.bytes(), &point.x, 3, 1, kFar);
        ok = false;
    } else {
        if (!player) return true;
        if (CanReachTarget(order) && CanPlanAction(order)) {
            // The action can be done from here.
            PathMessage(unit, route.bytes(), &point.x, 3, 0, kFar);
            if ((shout <= 0x29 || shout == 0x30) && shout != 0xFFFFFFFFu && unit->player)
                PlayerMessage(PlayerConnection(unit->player), nullptr, unit, static_cast<int>(shout));
            return true;
        }
        int kind = order->kind == kOrderAttack ? 2 : 1;
        if (order->kind == kOrderCastAt || order->kind == kOrderCast) {
            std::uint32_t source = action.source;
            void* spell = nullptr;
            bool skip = false;
            if (source & kQuickItemSource) {
                CItem* carrier = SpellCarrier(source);
                if (!carrier) skip = true;
                else spell = carrier->spell_;
            } else {
                spell = unit->GetSpellObject(source);
            }
            if (!skip && spell && reinterpret_cast<const itemdb::SpellRecord*>(static_cast<CSpell*>(spell)->GetPrototype())->castType == 0) kind = 2;
        }
        // Where the unit would stand at the end of its way, and whether it could act from there.
        auto actsFromDestination = [&]() {
            Vector3 destination;
            const Vector3* at = static_cast<AIPath*>(route.bytes())->Destination(&destination);
            if (!CanReachTarget(order)) return false;
            float x = unit->position.x, y = unit->position.y, z = unit->position.z;
            unit->position.x = at->x;
            unit->position.y = at->y;
            unit->position.z = at->z;
            bool can = CanPlanAction(order);
            unit->position.x = x;
            unit->position.y = y;
            unit->position.z = z;
            return can;
        };
        if (order->flag18 == 0) {
            LocalPath path;
            CEngineObject* target = order->target;
            bool savedFlag = false;
            if (target) {
                savedFlag = PathFlag();
                PathFlag() = false;
                SectorMapRemove(ServerSectors(), nullptr, target);
            }
            void* world = ManagerContextOf(this);
            path.Path()->flag6E = unit->player == nullptr;
            std::uint32_t saved43C = unit->altPassCosts;
            unit->altPassCosts = 1;
            bool found = MapFindPath(MapOf(world), nullptr, unit, &point, path.bytes, kFar, 0.0f);
            unit->altPassCosts = saved43C;
            if (!found) path.Path()->StandStill(unit);
            if (target) {
                SectorMapAdd(ServerSectors(), nullptr, target);
                PathFlag() = savedFlag;
            }
            path.Path()->ToMotionOrder(static_cast<AIPath*>(route.bytes()));   // a motion order is an AIPath's first part
            ok = actsFromDestination();
            if (ok) best = static_cast<float>(TimeUntilAction(order, route.bytes()));
        } else {
            ok = actsFromDestination();
        }
        PathMessage(unit, route.bytes(), &point.x, kind, ok ? 0 : 1, best);
        if (ok) {
            Shout(shout);
            return true;
        }
    }
    Shout(order->kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
    return ok;
}
EI_REPLACE_METHOD(0x0054E600, CUnitServer::PlanOrderRoute);

// --- A party unit's move -----------------------------------------------------------------------

void CUnitServer::PlanMove(const float* point, int moving, int run, float range) {
    void* world = ManagerContextOf(this);
    LocalPath path;
    UnitPtrList lifted{mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    LiftNearby(&lifted, point, static_cast<char>(moving));
    int mode = unit->moveMode;
    CSectorMap* sectors = static_cast<CWorldServer*>(world)->Sectors();
    CAIMap* map = MapOf(world);
    bool running = static_cast<std::uint8_t>(run) != 0;
    if (running && mode != 2 && mode != 3) {
        // A runner plans as walking (mode 2) when it may leave its cell that way.
        SectorMapRemove(sectors, nullptr, unit);
        unit->moveMode = 2;
        unit->UpdateSizeClass();
        if (!static_cast<CAIMap*>(map)->CanLeaveCell(unit)) unit->moveMode = mode;
        SectorMapAdd(sectors, nullptr, unit);
    }
    std::uint32_t flag = static_cast<CAIMap*>(map)->CellFlag80At(&unit->position);
    void* world2 = ManagerContextOf(this);
    path.Path()->flag6E = unit->player == nullptr;
    std::uint32_t saved43C = unit->altPassCosts;
    unit->altPassCosts = flag & 0xFF;
    bool found = MapFindPath(MapOf(world2), nullptr, unit, point, path.bytes, range, 0.0f);
    unit->altPassCosts = saved43C;
    if (running) {
        SectorMapRemove(sectors, nullptr, unit);
        unit->moveMode = mode;
        unit->UpdateSizeClass();
        SectorMapAdd(sectors, nullptr, unit);
    }
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        SectorMapAdd(sectors, nullptr, current->data);
    }
    ReportPath(path.bytes, found ? 1 : 0);
    // The list's RemoveAll (inlined).
    lifted.count = 0;
    lifted.free = nullptr;
    lifted.tail = nullptr;
    lifted.head = nullptr;
    PlexFreeDataChain(lifted.blocks);
    lifted.blocks = nullptr;
}
EI_REPLACE_METHOD(0x00554830, CUnitServer::PlanMove);
