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

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

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
constexpr int kSlotAimPoint = 0xD4 / 4;           // Vector3* (Vector3* out)
constexpr int kSlotEyePoint = 0xC0 / 4;           // Vector3* (Vector3* out)
constexpr int kSlotUseSpots = 0xB0 / 4;           // (CArray<Vector3>* out, float size)
constexpr int kSlotSize = 0xF0 / 4;               // ST0 ()

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

__declspec(naked) double __fastcall UnitGroundHeight(CUnit*, void*, float, float) EI_ORIGINAL(0x00530280)
__declspec(naked) void* __fastcall UnitSpellObject(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x00531A10)
__declspec(naked) char* __fastcall SpellPrototype(void*) EI_ORIGINAL(0x0068BA20)
// The AI map (world + 0x170): 005BB350 FindPath, 005BA830 CanLeaveCell, 005BBC70 the cell flag
// 0x80 under a point, 005BC3E0 LineOfSight (ST0).
__declspec(naked) bool __fastcall MapCanLeaveCell(void*, void*, CUnit*) EI_ORIGINAL(0x005BA830)
__declspec(naked) std::uint8_t __fastcall MapCellFlag80(void*, void*, const float*) EI_ORIGINAL(0x005BBC70)
__declspec(naked) double __fastcall MapLineOfSight(void*, void*, const float*, const float*) EI_ORIGINAL(0x005BC3E0)
// The sector map (world + 0x294 = 007B01F4): 005C1010 Remove, 005C0DF0 Add.
// AIPath (ai_motion.cpp / ai_path_stand.cpp): 005B23A0 Clear, 005B2190 stand still where the unit
// is, 005B22C0 the path as a motion order, 005C7D00 Destination (of a motion order).
__declspec(naked) void __fastcall PathStandStill(void*, void*, CUnit*) EI_ORIGINAL(0x005B2190)
__declspec(naked) float* __fastcall MotionDestination(void*, void*, float* /*out*/) EI_ORIGINAL(0x005C7D00)
// UnitMotion (ai_motion.cpp): 005C6270 Clear, 005C67D0 Build, 005C7430 Sample.
__declspec(naked) int __fastcall MotionClear(void*) EI_ORIGINAL(0x005C6270)
// The controller's functions rewritten elsewhere, through their game addresses: 0054C800
// CanReachTarget, 0054C6B0 CanPlanAction, 0054BCB0 FindSightTarget (combat_sight.cpp), 0054C070
// ActionDistance (ST0), 0054B7C0 SpellCarrier (combat.cpp), 00552780 Shout (unit_create.cpp),
// 005468D0 LiftNearby, 0054EB30 ReportPath (combat_motion.cpp), and this file's functions.
__declspec(naked) bool __fastcall CtlCanPlanAction(CUnitServer*, void*, const UnitAction*) EI_ORIGINAL(0x0054C6B0)
__declspec(naked) bool __fastcall CtlFindSightTarget(CUnitServer*, void*, const UnitAction*, SightTarget*)
    EI_ORIGINAL(0x0054BCB0)
__declspec(naked) CItem* __fastcall CtlSpellCarrier(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x0054B7C0)
__declspec(naked) void __fastcall CtlShout(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x00552780)
__declspec(naked) bool __fastcall CtlFindUseSpot(CUnitServer*, void*, UnitAction*, void*) EI_ORIGINAL(0x0054DF50)
__declspec(naked) double __fastcall CtlTimeUntilAction(CUnitServer*, void*, const UnitAction*, void*)
    EI_ORIGINAL(0x0054E250)
// 0066D490: the player's message about a path (combat_motion.cpp).
__declspec(naked) void __fastcall PlayerPathMessage(void*, void*, CUnit*, const void* /*motionOrder*/, const float*,
                                                    int /*kind*/, int /*flag*/, float /*limit*/) EI_ORIGINAL(0x0066D490)
// Containers: 00545750 a path cell list's constructor (blocks of 0x80), 00539890 its destructor,
// 00557230 / 00557250 a point array's, the UnitMotion members' 00556D70 / 00556810 / 004D5990 /
// 0054E5D0 and 004CB050; MFC 006FE90F CPlex::FreeDataChain.
__declspec(naked) void __fastcall CellListConstruct(void*) EI_ORIGINAL(0x00545750)
__declspec(naked) void __fastcall PointArrayConstruct(void*) EI_ORIGINAL(0x00557230)
__declspec(naked) void __fastcall PointArrayDestroy(void*) EI_ORIGINAL(0x00557250)
__declspec(naked) void __fastcall MotionPointsConstruct(void*) EI_ORIGINAL(0x00556D70)
__declspec(naked) void __fastcall MotionWaypointsConstruct(void*) EI_ORIGINAL(0x00556810)
__declspec(naked) void __fastcall MotionSpeedsConstruct(void*) EI_ORIGINAL(0x004D5990)
__declspec(naked) void __fastcall MotionCurvesConstruct(void*) EI_ORIGINAL(0x0054E5D0)
__declspec(naked) void __fastcall MotionSpeedsDestroy(void*) EI_ORIGINAL(0x004CB050)
__declspec(naked) void __fastcall PlexFreeDataChain(void*) EI_ORIGINAL(0x006FE90F)

std::uint8_t& PathFlag() { return *reinterpret_cast<std::uint8_t*>(0x007B00D2); }   // cleared around path searches
void* const kSectorMap = reinterpret_cast<void*>(0x007B01F4);
float HalfCell() { return *reinterpret_cast<const float*>(0x0073EEE4) * *reinterpret_cast<const float*>(0x0073B86C); }

// An AIPath on the stack: its lists and point array constructed, the order cleared.
struct LocalPath {
    alignas(4) std::uint8_t bytes[0x88];
    LocalPath() {
        CellListConstruct(bytes + 0x20);
        CellListConstruct(bytes + 0x3C);
        PointArrayConstruct(bytes + 0x70);
        PathClear(bytes);
    }
    ~LocalPath() {
        PointArrayDestroy(bytes + 0x70);
        CellListDestroy(bytes + 0x3C);
        CellListDestroy(bytes + 0x20);
    }
    LocalPath(const LocalPath&) = delete;
    LocalPath& operator=(const LocalPath&) = delete;
    float TargetX() const { return *reinterpret_cast<const float*>(bytes + 0x58); }
    float TargetY() const { return *reinterpret_cast<const float*>(bytes + 0x5C); }
    int Cost() const { return *reinterpret_cast<const int*>(bytes + 0x84); }
};

// A motion order on the stack (the order and its step list), as 005B22C0 fills it. The order
// part is zeroed (the original leaves it uninitialised until it is filled).
struct LocalMotionOrder {
    alignas(4) std::uint8_t bytes[0x3C];
    LocalMotionOrder() {
        std::memset(bytes, 0, 0x20);
        CellListConstruct(bytes + 0x20);
    }
    ~LocalMotionOrder() { CellListDestroy(bytes + 0x20); }
    LocalMotionOrder(const LocalMotionOrder&) = delete;
    LocalMotionOrder& operator=(const LocalMotionOrder&) = delete;
};

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
        MotionClear(&motion);
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
struct SpotArray {
    void* vtable;
    float (*data)[3];
    int size;
    int capacity;
    int growBy;
};

// A CPtrList on the stack (vtable 0073D454, blocks of 10).
struct UnitPtrList {
    void* vtable;
    struct Node {
        Node* next;
        Node* prev;
        CEngineObject* data;
    }* head;
    Node* tail;
    int count;
    Node* free;
    void* blocks;
    int blockSize;
};

void PathMessage(CUnit* unit, const void* motion, const float* point, int kind, int flag, float limit) {
    if (unit->player) PlayerPathMessage(At<void*>(unit->player, 0x20), nullptr, unit, motion, point, kind, flag, limit);
}

} // namespace

// --- A use order's spot -----------------------------------------------------------------------

bool CUnitServer::FindUseSpot(UnitAction* order, void* out) {
    void* world = ObjectWorld(this);
    order->point[2] = std::bit_cast<float>(kNoZBits);
    if (order->kind != kOrderSkill || !order->target || order->source != 0) return true;
    SpotArray spots{reinterpret_cast<void*>(0x0073C120), nullptr, 0, 0, 0};
    float size = static_cast<float>(ei::VCall<double>(unit, kSlotSize));
    ei::VCall<void>(order->target, kSlotUseSpots, &spots, size);
    bool result = true;
    if (spots.size > 0) {
        LocalPath path;
        int best = kNoSpot;
        float spot[3];
        float bestSpot[3] = {0.0f, 0.0f, 0.0f};
        void* map = static_cast<char*>(world) + 0x170;
        for (int i = 0; i < spots.size; ++i) {
            std::memcpy(spot, spots.data[i], sizeof(spot));
            PathClear(path.bytes);
            if (!MapFindPath(map, nullptr, unit, spot, path.bytes, kFar, 0.0f)) continue;
            // The path must end within 0073EF28 of the spot.
            float half = HalfCell();
            float ey = half * path.TargetY() - spot[1];
            float ex = half * path.TargetX() - spot[0];
            float limit = *reinterpret_cast<const float*>(0x0073EF28);
            if (!(ex * ex + ey * ey < limit * limit)) continue;
            if (path.Cost() >= best) continue;
            best = path.Cost();
            std::memcpy(bestSpot, spot, sizeof(bestSpot));
            if (out) PathToMotionOrder(path.bytes, nullptr, out);
        }
        if (best == kNoSpot) {
            result = false;
        } else {
            order->flag18 = 1;
            std::memcpy(order->point, bestSpot, sizeof(bestSpot));
        }
    }
    spots.vtable = reinterpret_cast<void*>(0x0073C120);
    if (spots.data) gamecrt::operator_delete(spots.data);
    return result;
}
EI_REPLACE_METHOD(0x0054DF50, CUnitServer::FindUseSpot);

// --- The time along a path until an action is possible -----------------------------------------

double CUnitServer::TimeUntilAction(const UnitAction* order, void* motionOrder) {
    if (At<int>(motionOrder, 0) != kMotionPath) return 0.0f;
    ObjectWorld(this);
    LocalUnitMotion simulated;
    MotionBuild(&simulated.motion, nullptr, motionOrder);
    SightTarget sight;
    bool canSee = CtlFindSightTarget(this, nullptr, order, &sight);
    float previous[3] = {0.0f, 0.0f, -1000.0f};
    float aim[3] = {0.0f, 0.0f, -1000.0f};
    double distance = CtlActionDistance(this, nullptr, order);
    float reachSq = static_cast<float>(distance * distance);
    if (!order->target) std::memcpy(aim, order->point, sizeof(aim));
    else ei::VCall<void>(order->target, kSlotAimPoint, aim);
    float t = 0.0f;
    for (;;) {
        float time = static_cast<float>(At<int>(motionOrder, 0x10)) + t;
        bool turning;
        float position[3];
        float direction[3];
        float speed;
        int cellX;
        int cellY;
        MotionSample(&simulated.motion, nullptr, nullptr, motionOrder, time, &turning, position, direction, &speed, &cellX,
                     &cellY);
        if (position[0] == previous[0] && position[1] == previous[1] && position[2] == previous[2]) break;
        std::memcpy(previous, position, sizeof(previous));
        float half = HalfCell();
        position[0] = half * position[0];
        position[1] = half * position[1];
        float dx = position[0] - aim[0];
        float dy = position[1] - aim[1];
        if (dy * dy + dx * dx > reachSq) {
            t += 5.0f;
            continue;
        }
        // In reach: done unless a line of sight is needed and missing from here.
        if (!canSee) break;
        if (sight.unit) {
            float eye[3];
            const float* at = ei::VCall<const float*>(sight.unit, kSlotEyePoint, eye);
            std::memcpy(sight.point, at, sizeof(sight.point));
            sight.unit = nullptr;
        }
        float sy = sight.point[1] - position[1];
        float sx = sight.point[0] - position[0];
        if (!(sy * sy + sx * sx >= sight.minDistanceSq)) break;   // C0: closer (or unordered)
        float from[3] = {position[0], position[1], position[2]};
        from[2] = static_cast<float>(UnitGroundHeight(unit, nullptr, position[0], position[1]) + sight.eyeHeight);
        double visible = MapLineOfSight(static_cast<char*>(ManagerContextOf(this)) + 0x170, nullptr, from, sight.point);
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
    LocalMotionOrder motion;
    float point[3];
    if (!order->target) std::memcpy(point, order->point, sizeof(point));
    else ei::VCall<void>(order->target, kSlotAimPoint, point);
    bool ok;
    if (!CtlFindUseSpot(this, nullptr, order, motion.bytes)) {
        PathMessage(unit, motion.bytes, point, 3, 1, kFar);
        ok = false;
    } else {
        if (!player) return true;
        if (CtlCanReachTarget(this, nullptr, order) && CtlCanPlanAction(this, nullptr, order)) {
            // The action can be done from here.
            PathMessage(unit, motion.bytes, point, 3, 0, kFar);
            if ((shout <= 0x29 || shout == 0x30) && shout != 0xFFFFFFFFu && unit->player)
                PlayerMessage(At<void*>(unit->player, 0x20), nullptr, unit, static_cast<int>(shout));
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
                else spell = At<void*>(carrier, 0x44);
            } else {
                spell = UnitSpellObject(unit, nullptr, source);
            }
            if (!skip && spell && At<int>(SpellPrototype(spell), 0x10) == 0) kind = 2;
        }
        // Where the unit would stand at the end of its way, and whether it could act from there.
        auto actsFromDestination = [&]() {
            float destination[3];
            const float* at = MotionDestination(motion.bytes, nullptr, destination);
            if (!CtlCanReachTarget(this, nullptr, order)) return false;
            float x = unit->positionX, y = unit->positionY, z = unit->positionZ;
            unit->positionX = at[0];
            unit->positionY = at[1];
            unit->positionZ = at[2];
            bool can = CtlCanPlanAction(this, nullptr, order);
            unit->positionX = x;
            unit->positionY = y;
            unit->positionZ = z;
            return can;
        };
        if (order->flag18 == 0) {
            LocalPath path;
            CEngineObject* target = order->target;
            std::uint8_t savedFlag = 0;
            if (target) {
                savedFlag = PathFlag();
                PathFlag() = 0;
                SectorMapRemove(AsSectorMap(kSectorMap), nullptr, target);
            }
            void* world = ManagerContextOf(this);
            path.bytes[0x6E] = unit->player == nullptr ? 1 : 0;
            std::uint32_t saved43C = At<std::uint32_t>(unit, 0x43C);
            At<std::uint32_t>(unit, 0x43C) = 1;
            bool found = MapFindPath(static_cast<char*>(world) + 0x170, nullptr, unit, point, path.bytes, kFar, 0.0f);
            At<std::uint32_t>(unit, 0x43C) = saved43C;
            if (!found) PathStandStill(path.bytes, nullptr, unit);
            if (target) {
                SectorMapAdd(AsSectorMap(kSectorMap), nullptr, target);
                PathFlag() = savedFlag;
            }
            PathToMotionOrder(path.bytes, nullptr, motion.bytes);
            ok = actsFromDestination();
            if (ok) best = static_cast<float>(CtlTimeUntilAction(this, nullptr, order, motion.bytes));
        } else {
            ok = actsFromDestination();
        }
        PathMessage(unit, motion.bytes, point, kind, ok ? 0 : 1, best);
        if (ok) {
            CtlShout(this, nullptr, shout);
            return true;
        }
    }
    CtlShout(this, nullptr, order->kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
    return ok;
}
EI_REPLACE_METHOD(0x0054E600, CUnitServer::PlanOrderRoute);

// --- A party unit's move -----------------------------------------------------------------------

void CUnitServer::PlanMove(const float* point, int moving, int run, float range) {
    void* world = ManagerContextOf(this);
    LocalPath path;
    UnitPtrList lifted{reinterpret_cast<void*>(0x0073D454), nullptr, nullptr, 0, nullptr, nullptr, 10};
    CtlLiftNearby(this, nullptr, &lifted, point, moving);
    int mode = unit->moveMode;
    void* sectors = static_cast<char*>(world) + 0x294;
    void* map = static_cast<char*>(world) + 0x170;
    bool running = static_cast<std::uint8_t>(run) != 0;
    if (running && mode != 2 && mode != 3) {
        // A runner plans as walking (mode 2) when it may leave its cell that way.
        SectorMapRemove(AsSectorMap(sectors), nullptr, unit);
        unit->moveMode = 2;
        UnitUpdateSizeClass(unit);
        if (!MapCanLeaveCell(map, nullptr, unit)) unit->moveMode = mode;
        SectorMapAdd(AsSectorMap(sectors), nullptr, unit);
    }
    std::uint32_t flag = MapCellFlag80(map, nullptr, &unit->positionX);
    void* world2 = ManagerContextOf(this);
    path.bytes[0x6E] = unit->player == nullptr ? 1 : 0;
    std::uint32_t saved43C = At<std::uint32_t>(unit, 0x43C);
    At<std::uint32_t>(unit, 0x43C) = flag & 0xFF;
    bool found = MapFindPath(static_cast<char*>(world2) + 0x170, nullptr, unit, point, path.bytes, range, 0.0f);
    At<std::uint32_t>(unit, 0x43C) = saved43C;
    if (running) {
        SectorMapRemove(AsSectorMap(sectors), nullptr, unit);
        unit->moveMode = mode;
        UnitUpdateSizeClass(unit);
        SectorMapAdd(AsSectorMap(sectors), nullptr, unit);
    }
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        SectorMapAdd(AsSectorMap(sectors), nullptr, current->data);
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
