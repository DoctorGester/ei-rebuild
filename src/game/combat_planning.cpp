// Combat, eighth part: planning an order's way (PlanOrder), the spot a use order goes to, the time
// along a path until an action can be carried out, and a party unit's move. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Fourth round").
//
// These build paths on the stack: an AIPath (0x88 bytes: the motion order, the step and cell lists
// at +0x20 / +0x3C, a point array at +0x70) and the motion order a path turns into (0x3C bytes:
// the order and a step list) for the player's path messages and the simulation of a unit's motion.
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "ai_motion.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "player_messages.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_effects.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"

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
void AimPointInto(CEngineObject* object, float* out) { Slots(object)->AimPoint(reinterpret_cast<Vector3*>(out)); }

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

inline double __fastcall UnitGroundHeight(CUnit* a0, void*, float a2, float a3) { return UnitGroundHeightOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void* __fastcall UnitSpellObject(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x00531A10)
// The AI map (world + 0x170): 005BB350 FindPath, 005BA830 CanLeaveCell, 005BBC70 the cell flag
// 0x80 under a point, 005BC3E0 LineOfSight (ST0).
// The sector map (world + 0x294 = 007B01F4): 005C1010 Remove, 005C0DF0 Add.
// AIPath (ai_motion.cpp / ai_path_stand.cpp): 005B23A0 Clear, 005B2190 stand still where the unit
// is, 005B22C0 the path as a motion order, 005C7D00 Destination (of a motion order).
__declspec(naked) void __fastcall PathStandStill(void*, void*, CUnit*) EI_ORIGINAL(0x005B2190)
__declspec(naked) Vector3* __fastcall MotionDestination(void*, void*, Vector3* /*out*/) EI_ORIGINAL(0x005C7D00)
// UnitMotion (ai_motion.cpp): 005C67D0 Build, 005C7430 Sample (005C6270 Clear is in originals_ai.h).
// The controller's functions rewritten elsewhere, through their game addresses: 0054C800
// CanReachTarget, 0054C6B0 CanPlanAction, 0054BCB0 FindSightTarget (combat_sight.cpp), 0054C070
// ActionDistance (ST0), 0054B7C0 SpellCarrier (combat.cpp), 00552780 Shout (unit_create.cpp),
// 005468D0 LiftNearby, 0054EB30 ReportPath (combat_motion.cpp), and this file's functions.
__declspec(naked) CItem* __fastcall CtlSpellCarrier(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x0054B7C0)
__declspec(naked) bool __fastcall CtlFindUseSpot(CUnitServer*, void*, UnitAction*, void*) EI_ORIGINAL(0x0054DF50)
__declspec(naked) double __fastcall CtlTimeUntilAction(CUnitServer*, void*, const UnitAction*, void*)
    EI_ORIGINAL(0x0054E250)
// 0066D490: the player's message about a path (combat_motion.cpp).
void __fastcall PlayerPathMessage(void* a0, void*, CUnit* a2, const void* a3, const float* a4, int a5, int a6, float a7) {
    PlayerPathMessageOriginal(a0, nullptr, a2, a3, a4, a5, a6, std::bit_cast<std::uint32_t>(a7));
}
// Containers: 00545750 a path cell list's constructor (blocks of 0x80), 00539890 its destructor,
// 00557230 / 00557250 a point array's, the UnitMotion members' 00556D70 / 00556810 / 004D5990 /
// 0054E5D0 and 004CB050; MFC 006FE90F CPlex::FreeDataChain.
__declspec(naked) void __fastcall CellListConstruct(void*) EI_ORIGINAL(0x00545750)
void __fastcall PointArrayConstruct(void* a0) { PointArrayConstructOriginal(a0); }
__declspec(naked) void __fastcall PointArrayDestroy(void*) EI_ORIGINAL(0x00557250)
__declspec(naked) void __fastcall MotionPointsConstruct(void*) EI_ORIGINAL(0x00556D70)
__declspec(naked) void __fastcall MotionWaypointsConstruct(void*) EI_ORIGINAL(0x00556810)
__declspec(naked) void __fastcall MotionSpeedsConstruct(void*) EI_ORIGINAL(0x004D5990)
__declspec(naked) void __fastcall MotionCurvesConstruct(void*) EI_ORIGINAL(0x0054E5D0)
__declspec(naked) void __fastcall MotionSpeedsDestroy(void*) EI_ORIGINAL(0x004CB050)
inline void __fastcall PlexFreeDataChain(void* p) { PlexFreeDataChainOriginal(p); }

// The server's AI map's refreshUnits (007B00D2), cleared around path searches, and its sector
// map (007B01F4).
bool& PathFlag() { return WorldServer()->AIMap()->refreshUnits; }
CSectorMap* ServerSectors() { return WorldServer()->Sectors(); }
CAIMap* MapOf(void* world) { return static_cast<CWorldServer*>(world)->AIMap(); }
float HalfCell() { return *reinterpret_cast<const float*>(0x0073EEE4) * *reinterpret_cast<const float*>(0x0073B86C); }

// An AIPath on the stack: its lists and point array constructed, the order cleared.
struct LocalPath {
    alignas(4) std::uint8_t bytes[sizeof(AIPath)];
    AIPath* Path() { return reinterpret_cast<AIPath*>(bytes); }
    const AIPath* Path() const { return reinterpret_cast<const AIPath*>(bytes); }
    LocalPath() {
        CellListConstruct(&Path()->steps);
        CellListConstruct(&Path()->cells);
        PointArrayConstruct(&Path()->excludedBlocks);
        PathClear(bytes);
    }
    ~LocalPath() {
        PointArrayDestroy(&Path()->excludedBlocks);
        CellListDestroy(&Path()->cells);
        CellListDestroy(&Path()->steps);
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
    alignas(4) std::uint8_t steps[sizeof(PathCellList)];
    LocalMotionOrder() {
        std::memset(&order, 0, sizeof(order));
        CellListConstruct(steps);
    }
    ~LocalMotionOrder() { CellListDestroy(steps); }
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
        MotionPointsConstruct(&motion.points);
        motion.points.vtable = reinterpret_cast<void*>(0x0073EB9C);
        MotionWaypointsConstruct(&motion.waypoints);
        MotionSpeedsConstruct(&motion.stepSpeeds);
        MotionCurvesConstruct(&motion.fromX);
        UnitMotionClearOriginal(&motion);
        motion.speedScale = *reinterpret_cast<const float*>(0x0073EF20);
        motion.segment = -1;
        motion.baseTurnRate = std::bit_cast<float>(0x501502F9u);   // 1e10
    }
    ~LocalUnitMotion() {
        MotionSpeedsDestroy(&motion.stepSpeeds);
        motion.waypoints.vtable = reinterpret_cast<void*>(0x0073EBB4);
        if (motion.waypoints.data) gamecrt::operator_delete(motion.waypoints.data);
        motion.points.vtable = reinterpret_cast<void*>(0x0073EBA8);
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
    if (unit->player) PlayerPathMessage(PlayerConnection(unit->player), nullptr, unit, motion, point, kind, flag, limit);
}

} // namespace

// --- A use order's spot -----------------------------------------------------------------------

bool CUnitServer::FindUseSpot(UnitAction* order, void* out) {
    void* world = ObjectWorld(this);
    order->point.z = std::bit_cast<float>(kNoZBits);
    if (order->kind != kOrderSkill || !order->target || order->source != 0) return true;
    SpotArray spots{reinterpret_cast<void*>(0x0073C120), nullptr, 0, 0, 0};
    float size = static_cast<float>(Slots(unit)->Radius());
    Slots(order->target)->UseSpots(&spots, size);
    bool result = true;
    if (spots.size > 0) {
        LocalPath path;
        int best = kNoSpot;
        Vector3 spot;
        float bestSpot[3] = {0.0f, 0.0f, 0.0f};
        CAIMap* map = MapOf(world);
        for (int i = 0; i < spots.size; ++i) {
            std::memcpy(&spot, spots.data[i], sizeof(spot));
            PathClear(path.bytes);
            if (!MapFindPath(map, nullptr, unit, &spot, path.bytes, kFar, 0.0f)) continue;
            // The path must end within 0073EF28 of the spot.
            float half = HalfCell();
            float ey = half * path.TargetY() - spot.y;
            float ex = half * path.TargetX() - spot.x;
            float limit = *reinterpret_cast<const float*>(0x0073EF28);
            if (!(LengthSquared2DUnrounded(ex, ey) < limit * limit)) continue;
            if (path.Cost() >= best) continue;
            best = path.Cost();
            std::memcpy(bestSpot, &spot, sizeof(bestSpot));
            if (out) PathToMotionOrder(path.bytes, nullptr, out);
        }
        if (best == kNoSpot) {
            result = false;
        } else {
            order->flag18 = 1;
            std::memcpy(&order->point, bestSpot, sizeof(bestSpot));
        }
    }
    spots.vtable = reinterpret_cast<void*>(0x0073C120);
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
    MotionBuild(&simulated.motion, nullptr, motionOrder);
    SightTarget sight;
    bool canSee = CtlFindSightTarget(this, nullptr, order, &sight);
    float previous[3] = {0.0f, 0.0f, -1000.0f};
    float aim[3] = {0.0f, 0.0f, -1000.0f};
    double distance = CtlActionDistance(this, nullptr, order);
    float reachSq = static_cast<float>(distance * distance);
    if (!order->target) std::memcpy(aim, &order->point, sizeof(aim));
    else AimPointInto(order->target, aim);
    float t = 0.0f;
    for (;;) {
        float time = static_cast<float>(pathOrder->startTime) + t;
        bool turning;
        Vector3 position;
        Vector3 direction;
        float speed;
        int cellX;
        int cellY;
        MotionSample(&simulated.motion, nullptr, nullptr, motionOrder, time, &turning, &position, &direction, &speed, &cellX,
                     &cellY);
        if (position.x == previous[0] && position.y == previous[1] && position.z == previous[2]) break;
        std::memcpy(previous, &position, sizeof(previous));
        float half = HalfCell();
        position.x = half * position.x;
        position.y = half * position.y;
        float dx = position.x - aim[0];
        float dy = position.y - aim[1];
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
        if (!(LengthSquared2DUnrounded(sy, sx) >= sight.minDistanceSq)) break;   // C0: closer (or unordered)
        Vector3 from = {position.x, position.y, position.z};
        from.z = static_cast<float>(UnitGroundHeight(unit, nullptr, position.x, position.y) + sight.eyeHeight);
        double visible = LineOfSightOriginal(MapOf(ManagerContextOf(this)), nullptr, &from, &sight.point);
        if (!(visible < *reinterpret_cast<const float*>(0x0073EEC0))) break;
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
    else AimPointInto(order->target, &point.x);
    bool ok;
    if (!CtlFindUseSpot(this, nullptr, order, route.bytes())) {
        PathMessage(unit, route.bytes(), &point.x, 3, 1, kFar);
        ok = false;
    } else {
        if (!player) return true;
        if (CtlCanReachTarget(this, nullptr, order) && CtlCanPlanAction(this, nullptr, order)) {
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
                CItem* carrier = CtlSpellCarrier(this, nullptr, source);
                if (!carrier) skip = true;
                else spell = carrier->spell_;
            } else {
                spell = UnitSpellObject(unit, nullptr, source);
            }
            if (!skip && spell && reinterpret_cast<const itemdb::SpellRecord*>(SpellGetPrototypeOriginal(static_cast<CSpell*>(spell)))->unknown_10 == 0) kind = 2;
        }
        // Where the unit would stand at the end of its way, and whether it could act from there.
        auto actsFromDestination = [&]() {
            Vector3 destination;
            const Vector3* at = MotionDestination(route.bytes(), nullptr, &destination);
            if (!CtlCanReachTarget(this, nullptr, order)) return false;
            float x = unit->position.x, y = unit->position.y, z = unit->position.z;
            unit->position.x = at->x;
            unit->position.y = at->y;
            unit->position.z = at->z;
            bool can = CtlCanPlanAction(this, nullptr, order);
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
            std::uint32_t saved43C = unit->unknown_43C;
            unit->unknown_43C = 1;
            bool found = MapFindPath(MapOf(world), nullptr, unit, &point, path.bytes, kFar, 0.0f);
            unit->unknown_43C = saved43C;
            if (!found) PathStandStill(path.bytes, nullptr, unit);
            if (target) {
                SectorMapAdd(ServerSectors(), nullptr, target);
                PathFlag() = savedFlag;
            }
            PathToMotionOrder(path.bytes, nullptr, route.bytes());
            ok = actsFromDestination();
            if (ok) best = static_cast<float>(CtlTimeUntilAction(this, nullptr, order, route.bytes()));
        } else {
            ok = actsFromDestination();
        }
        PathMessage(unit, route.bytes(), &point.x, kind, ok ? 0 : 1, best);
        if (ok) {
            ControllerShout(this, nullptr, shout);
            return true;
        }
    }
    ControllerShout(this, nullptr, order->kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
    return ok;
}
EI_REPLACE_METHOD(0x0054E600, CUnitServer::PlanOrderRoute);

// --- A party unit's move -----------------------------------------------------------------------

void CUnitServer::PlanMove(const float* point, int moving, int run, float range) {
    void* world = ManagerContextOf(this);
    LocalPath path;
    UnitPtrList lifted{mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    CtlLiftNearby(this, nullptr, &lifted, point, moving);
    int mode = unit->moveMode;
    CSectorMap* sectors = static_cast<CWorldServer*>(world)->Sectors();
    CAIMap* map = MapOf(world);
    bool running = static_cast<std::uint8_t>(run) != 0;
    if (running && mode != 2 && mode != 3) {
        // A runner plans as walking (mode 2) when it may leave its cell that way.
        SectorMapRemove(sectors, nullptr, unit);
        unit->moveMode = 2;
        UnitUpdateSizeClass(unit);
        if (!AIMapCanLeaveCellOriginal(map, nullptr, unit)) unit->moveMode = mode;
        SectorMapAdd(sectors, nullptr, unit);
    }
    std::uint32_t flag = CellFlag80AtOriginal(map, nullptr, &unit->position);
    void* world2 = ManagerContextOf(this);
    path.Path()->flag6E = unit->player == nullptr;
    std::uint32_t saved43C = unit->unknown_43C;
    unit->unknown_43C = flag & 0xFF;
    bool found = MapFindPath(MapOf(world2), nullptr, unit, point, path.bytes, range, 0.0f);
    unit->unknown_43C = saved43C;
    if (running) {
        SectorMapRemove(sectors, nullptr, unit);
        unit->moveMode = mode;
        UnitUpdateSizeClass(unit);
        SectorMapAdd(sectors, nullptr, unit);
    }
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        SectorMapAdd(sectors, nullptr, current->data);
    }
    CtlReportPath(this, nullptr, path.bytes, found ? 1 : 0);
    // The list's RemoveAll (inlined).
    lifted.count = 0;
    lifted.free = nullptr;
    lifted.tail = nullptr;
    lifted.head = nullptr;
    PlexFreeDataChain(lifted.blocks);
    lifted.blocks = nullptr;
}
EI_REPLACE_METHOD(0x00554830, CUnitServer::PlanMove);
