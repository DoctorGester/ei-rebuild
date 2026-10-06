#pragma once

// Unit AI: the per-unit "brain" that picks a behaviour mode every AI tick and runs it, the modes
// themselves (Guard, Patrol, Sentry, Suspection, Aggression, CorpseWatcher, Player, Fear, Tamed,
// Follow; the script's UM* commands create them) and the actions the brain issues (move, attack,
// ...). Range 005D0000-005DC000 of game.exe. Names are provisional.
//
// Ownership: a unit's controller (CUnit + 0x240) embeds the brain at +0x980. The controller's tick
// (00552300) calls AIBrain::Think once per AI update. Modes are reference-counted objects (16-bit
// count at +4, released through 0040A3A0) kept in the brain's mode list; the brain holds a counted
// pointer to the mode in charge.

#include "ai_map.h"
#include "core_refcount.h"
#include "engine_object.h"
#include "maths.h"
#include "unit.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct AIBrain;
struct AIOrder;

// The stream the save-game code passes to Load/Save (vtable slot 1 Read, slot 2 Write); +8 is the
// version of the file being read.
class AIStream {
public:
    virtual void Slot0() = 0;
    virtual void Read(void* buffer, unsigned size) = 0;          // 1
    virtual void Write(const void* buffer, unsigned size) = 0;   // 2

    int unknown_04;
    int version;            // +0x08
};

// The unit controller of AI units (the object at CUnit + 0x240; its tick is 00552300). Only the
// fields the brain reads.
struct UnitController {
    std::uint8_t unknown_000[0x1C];
    CUnit* unit;                    // +0x01C
    std::uint8_t unknown_020[0x164 - 0x20];
    int state;                      // +0x164 5: the brain does nothing
    std::uint8_t unknown_168[0x174 - 0x168];
    std::uint8_t busy;              // +0x174 set by modes that keep the unit occupied
    std::uint8_t unknown_175[0x18C - 0x175];
    std::uint8_t inCombat;          // +0x18C
    std::uint8_t unknown_18D[0x190 - 0x18D];
    int command;                    // +0x190 the controller's current command (1/2 move, 3 attack, ...)
    std::uint8_t unknown_194[4];
    CEngineObject* commandTarget;   // +0x198
    std::uint8_t unknown_19C[0x1B4 - 0x19C];
    int nextCommand;                // +0x1B4 the queued command
    std::uint8_t unknown_1B8[4];
    CEngineObject* nextTarget;      // +0x1BC
    std::uint8_t unknown_1C0[0x1E0 - 0x1C0];
    UnitStats stats;                // +0x1E0
    std::uint16_t protection[8];    // +0x8E4 what an attack's damage words are measured against (00522AE0)
    std::uint8_t unknown_8F4[4];
    float moveFactors[4];           // +0x8F8 (CUnitServer::moveFactors)
    std::uint8_t unknown_908[0x978 - 0x908];
    // +0x978 called with the unit when its current action (an animation) has finished, or null.
    void(__fastcall* actionDone)(CUnit* unit);
    std::uint8_t unknown_97C[4];
    std::uint8_t brain[0x190];      // +0x980 the AIBrain (Brain())

    AIBrain* Brain();

    // 00552CF0 (RET 4; unit_ai_brain.cpp). The unit's CUnit::AddHostileMask (its EAX returned).
    std::uintptr_t AddHostileMask(std::uint32_t mask);

    // Not rewritten (forwarders).
    void Idle();                                            // 00555180
    void StopQueued();                                      // 00555200
    void Turn(const Vector3* direction, float time);        // 005546B0
    void Say(int what);                                     // 005AEA60 on this + 0xB10 (see Voice)
    void MoveTo(const Vector3* point, int run, int unused, float time);    // 00554740
    void Shout(int message);                                // 00552780 (party units only)
    // 00554B30. Starts attacking `target`.
    void Attack(CEngineObject* target, int how, int unknown, int flags);
    // 0054C8B0. Whether the player's order check accepts `order` (see AttackTarget).
    bool CheckOrder(const void* order);
    // 0054C960 (RET 8). The same check with the unit moved to `point` meanwhile (only AL).
    bool CheckOrderAt(const AIOrder* order, const Vector3* point);
    void CastAt(int spell, const Vector3* point, int run);  // 00554D00
    void TurnTo(const Vector3* point, int time, int unused, float limit);   // 00554830
    void UseOnPoint(CEngineObject* object, int a, int b);   // 00555570
    void UseOnUnit(CEngineObject* unit, int b);             // 005553B0
    void UseOnUnit2(CEngineObject* unit, int b);            // 00555480
    void UseSpecial(CEngineObject* unit, int b);            // 00555630
    bool CanCast(int spell);                                // 0054B820
    void Cast(int spell, CEngineObject* target, int unused);    // 00554F50
    // 0054B520 (CUnitServer::GetMovePoint). The point of the first move order; false without one.
    bool GetMovePoint(Vector3* out);
};
static_assert(offsetof(UnitController, unit) == 0x1C);
static_assert(offsetof(UnitController, state) == 0x164);
static_assert(offsetof(UnitController, busy) == 0x174);
static_assert(offsetof(UnitController, inCombat) == 0x18C);
static_assert(offsetof(UnitController, command) == 0x190);
static_assert(offsetof(UnitController, commandTarget) == 0x198);
static_assert(offsetof(UnitController, nextCommand) == 0x1B4);
static_assert(offsetof(UnitController, nextTarget) == 0x1BC);
static_assert(offsetof(UnitController, stats) == 0x1E0);
static_assert(offsetof(UnitController, protection) == 0x8E4);
static_assert(offsetof(UnitController, actionDone) == 0x978);
static_assert(offsetof(UnitController, brain) == 0x980);

// An order as the controller's order checks take it (0x1C bytes, built on the stack).
struct AIOrder {
    // 00545F40. Kind 9 (none): the object released and cleared, the flag cleared.
    void Clear();
    // 005CF720 (RET 4). Clear, then an attack (3, 6) on `object` (referenced).
    void MakeAttack(CEngineObject* object);
    // 005CF780 (RET 8). Clear, then a cast (5) of `spell` at `object` (referenced).
    void MakeCast(int spell, CEngineObject* object);

    int kind;                       // +0x00
    int what;                       // +0x04 6 for an attack, the spell for a cast
    CEngineObject* object;          // +0x08 counted
    float point[3];                 // +0x0C left uninitialised by the AI's orders
    std::uint8_t flag;              // +0x18
    std::uint8_t unknown_19[3];
};
static_assert(sizeof(AIOrder) == 0x1C);

// One remembered look direction of the current move (0x10 bytes).
struct AILook {
    float angle;                    // +0x00
    float time;                     // +0x04 turning time
    float wait;                     // +0x08 wait afterwards (game ticks)
    std::uint8_t stopFirst;         // +0x0C
    std::uint8_t unknown_0D[3];
};
static_assert(sizeof(AILook) == 0x10);

// CArray<AILook> (0x14 bytes; vtable 0073D4B4, base 0073D460). SetSize 005D8EE0 (new elements
// zeroed) and Copy 005D8B60 are registered on the template (unit_ai_looks.cpp). The modes call the
// template code through its addresses (the ...At forwarders, unit_ai.cpp) so the verify wrappers
// see their calls.
using AILookArray = mfc::CArray<AILook>;
void __fastcall LooksConstructAt(AILookArray* looks);                                // 005573F0
void __fastcall LooksSetSizeAt(AILookArray* looks, void*, int size, int growBy);    // 005D8EE0
void __fastcall LooksCopyAt(AILookArray* looks, void*, const AILookArray* other);   // 005D8B60
void __fastcall LooksDestroyAt(AILookArray* looks);                                  // 004CB770 (base vtable, frees the data)
void __fastcall LooksGetAt(const AILookArray* looks, void*, AILook* out, int index);  // 005D9DE0
// 005573F0 (unit_ai_looks.cpp). Base vtable, no data, growth step 0. Returns `looks`.
AILookArray* __fastcall LooksInit(AILookArray* looks);
// 005D9DE0. CArray::GetAt returning by value (into `out`, which EAX returns).
AILook* __fastcall LooksElementAt(const AILookArray* looks, void*, AILook* out, int index);
constexpr std::uintptr_t kLookArrayVtable = 0x0073D4B4;
constexpr std::uintptr_t kLookArrayBaseVtable = 0x0073D460;

// A move the modes prepare on the stack (0x20 bytes): where to go and what to look at there.
struct AIMoveOrder {
    void Reset();                                   // 00545B20 (point 0, looks emptied)
    // 00545B20 (unit_ai_looks.cpp; the modes call it through Reset, its address). The point zeroed,
    // the looks' data freed, size and capacity 0 (step and vtable kept).
    void Clear();

    Vector3 point;                  // +0x00
    AILookArray looks;              // +0x0C
};
static_assert(sizeof(AIMoveOrder) == 0x20);

// Mode types (vtable slot 8), as the brain names them in its "new motivation" message.
enum AIModeType {
    kModeGuard = 0,
    kModePatrol = 1,
    kModeSentry = 2,
    kModeSuspection = 3,
    kModeAggression = 4,
    kModeCorpseWatcher = 5,
    kModePlayer = 6,
    kModeFear = 7,
    kModeTamed = 8,
    kModeFollow = 10,
};

// The modes' vtable as virtual functions. A mode is a reference-counted object (core_refcount.h:
// slots 0-3 are CRefObject's, slot 2 the destructor body, 3 Clone), then:
class AIModeVirtuals : public CRefObject {
public:
    virtual int Evaluate(AIBrain* brain) = 0;           // 4: the priority the mode claims this tick
    virtual void Execute(AIBrain* brain) = 0;           // 5: runs while the mode is in charge
    virtual void Enter(AIBrain* brain) = 0;             // 6
    virtual void Leave(AIBrain* brain) = 0;             // 7
    virtual int GetType() = 0;                          // 8
    // 9: 1 for most modes; AddMode replaces a mode of another type when both answer 1.
    virtual bool Exclusive() = 0;
    virtual void OnObjectDeleted(CEngineObject* object) = 0;    // 10
    virtual void Load(AIStream* stream) = 0;            // 11
    virtual void Save(AIStream* stream) = 0;            // 12
};

// Base of all modes (vtables 0073D15C (abstract), 0073D22C (abstract)); the slots are listed in
// AIModeVirtuals. The structs below have no C++ virtuals (their constructors install the game's
// vtables); our slot versions are registered at the original addresses.
struct AIMode {
    void* vtable;
    std::int16_t refs;              // +0x04 (INC word / 0040A3A0)
    std::int16_t unknown_06;

    AIModeVirtuals* Virtuals() { return reinterpret_cast<AIModeVirtuals*>(this); }
    int Evaluate(AIBrain* brain);   // through the vtable
    void Execute(AIBrain* brain);
    void Enter(AIBrain* brain);
    void Leave(AIBrain* brain);
    int GetType();
    void OnObjectDeleted(CEngineObject* object);
    // 0040A3A0 (CRefObject::Release): drops a reference, deleting the mode at zero.
    void Release();
};
static_assert(sizeof(AIMode) == 8);

// The modes' vtables as their constructors install them (the script's 004BA1C0-004BA920).
namespace aimode {
inline void* const kBaseVtable = reinterpret_cast<void*>(0x0073D15C);
inline void* const kGuardVtable = reinterpret_cast<void*>(0x0073D128);
inline void* const kSentryVtable = reinterpret_cast<void*>(0x0073D190);
inline void* const kFollowVtable = reinterpret_cast<void*>(0x0073D1C4);
inline void* const kSuspectionVtable = reinterpret_cast<void*>(0x0073D1F8);
inline void* const kAggressionVtable = reinterpret_cast<void*>(0x0073D260);
inline void* const kCorpseWatcherVtable = reinterpret_cast<void*>(0x0073D294);
inline void* const kFearVtable = reinterpret_cast<void*>(0x0073FF8C);
} // namespace aimode

// Shared slot implementations of the simple modes (registered once, used by several vtables).
namespace aimode {
// 005D0A80. Slot 4 of Guard, Sentry, Follow and the abstract base: always 50.
int __fastcall Priority50(AIMode* self, void*, AIBrain* brain);
// 005D0AF0. Slot 6 of Guard, Patrol, Sentry, Follow and the abstract base: drop the action and
// target, pick a new alertness decay (0.8 + 0.4 * random) and clear the controller's busy flag.
void __fastcall EnterIdle(AIMode* self, void*, AIBrain* brain);
// 005D0A50. Slot 6 (Enter) of CorpseWatcher, Player and the abstract base 0073D22C: drop the
// action and target.
void __fastcall ClearAction(AIMode* self, void*, AIBrain* brain);
// 005D0A90 (unit_ai_mode_lifetime.cpp). Slot 5 (Execute) of the abstract base 0073D15C: the idle
// remark (1 in 1001), then LookAround(0).
void __fastcall LookAroundExecute(AIMode* self, void*, AIBrain* brain);
// 005D1210 / 005D2920. Slot 8 (GetType) of Patrol (1) and Fear (7).
int __fastcall PatrolType(AIMode* self);
int __fastcall FearType(AIMode* self);
// The root vtable a mode's destructor leaves (CRefObject's, 0073BB68).
inline void* const kRootVtable = reinterpret_cast<void*>(0x0073BB68);
inline void* const kPatrolVtable = reinterpret_cast<void*>(0x0073FF40);
inline void* const kPatrolPointsVtable = reinterpret_cast<void*>(0x0073FF74);
inline void* const kPatrolPointsBaseVtable = reinterpret_cast<void*>(0x0073FF80);
inline void* const kPlayerVtable = reinterpret_cast<void*>(0x0073F0F8);
inline void* const kActionBaseVtable = reinterpret_cast<void*>(0x0073D22C);
// 005D8850 / 005D6D10 (unit_ai_mode_lifetime.cpp). The abstract bases' constructors: the vtable
// 0073D15C / 0073D22C only; return the mode.
AIMode* __fastcall ConstructModeBase(AIMode* mode);
AIMode* __fastcall ConstructActionBase(AIMode* mode);
} // namespace aimode

// Guard (vtable 0073D128, 0x20 bytes): stay near a point.
struct GuardMode : AIMode {
    // 005D0B70. Sets the point and the two radii (negative: the globals 007C540C / 007C53FC).
    void Set(const Vector3* point, float radius, float wander);
    // 005D0BE0 (slot 5). When idle: walk to a free place near the point (after 100 failures, near
    // the unit) and look around there.
    void Execute(AIBrain* brain);
    // 005D0EB0 / 005D0F00 (slots 11 / 12).
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    Vector3 point;                  // +0x08
    float radius;                   // +0x14
    float wander;                   // +0x18
    int failures;                   // +0x1C
};
static_assert(sizeof(GuardMode) == 0x20);

// One patrol point (0x20 bytes): position and a CArray of 0x10-byte look/wait entries.
struct PatrolPoint {
    Vector3 position;               // +0x00
    AILookArray looks;              // +0x0C
};
static_assert(sizeof(PatrolPoint) == 0x20);

// CArray<PatrolPoint> (PatrolMode + 8, vtables 0073FF74 / 0073FF80; unit_ai_mode_lifetime.cpp).
using PatrolPointArray = mfc::CArray<PatrolPoint>;
// 005D8CF0. The base vtable, every point's looks freed, then the buffer (pointer and count kept).
void __fastcall PatrolPointsDestruct(PatrolPointArray* points);
// 005D8CD0 (RET 4). Destruct, then operator delete when `flags` bit 0 is set; returns `points`.
PatrolPointArray* __fastcall PatrolPointsDeletingDestruct(PatrolPointArray* points, void*, int flags);

// Patrol (vtable 0073FF40, 0x20 bytes): walk a list of points.
struct PatrolMode : AIMode {
    // 005D16B0 (slot 4). 50; without points: FatalError "Unit %i has no patrol points, switching to
    // guard", a point at the unit's position, and -1000.
    int Evaluate(AIBrain* brain);
    // 005D1700 (slot 5). When idle: on to the next point and its looks.
    void Execute(AIBrain* brain);
    // 005D1690. The last point, or null.
    PatrolPoint* LastPoint();
    // 005D12B0 (through its address, so the verify wrapper sees Evaluate's calls).
    void AddPoint(const Vector3* position);
    // 005D12B0 (unit_ai_looks.cpp). Appends a point with no looks at ((n + 0.5) / 2, z 0) for
    // n = 2x rounded to nearest (so 0..0.5 above `position`, not the middle of its half-cell).
    void AppendPoint(const Vector3* position);
    // unit_ai_mode_lifetime.cpp. 005D0F60: the vtables, no points, index 0 (+4 untouched); returns
    // this. 005D1240: frees every point's looks and the points (the array keeps its pointer and
    // count), the root vtable last. 005D1220 (slot 0, RET 4): the destructor, then operator
    // delete when `flags` bit 0 is set; returns this.
    PatrolMode* Construct();
    void Destruct();
    PatrolMode* DeletingDestruct(int flags);

    void* pointsVtable;             // +0x08 CArray (vtable 0073FF74 / 0073FF80)
    PatrolPoint* points;            // +0x0C
    int pointCount;                 // +0x10
    int pointMax;                   // +0x14
    int pointGrowBy;                // +0x18
    int current;                    // +0x1C
};
static_assert(sizeof(PatrolMode) == 0x20);

// Sentry (vtable 0073D190, 0x14 bytes): stand at a point.
struct SentryMode : AIMode {
    // 005D1B50. Sets the point.
    void Set(const Vector3* point);
    // 005D1B70 (slot 5). When idle: back to the point and look around.
    void Execute(AIBrain* brain);
    // 005D1DA0 / 005D1DC0 (slots 11 / 12).
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    Vector3 point;                  // +0x08
};
static_assert(sizeof(SentryMode) == 0x14);

// Follow (vtable 0073D1C4, 0x0C bytes): stay with a leader.
struct FollowMode : AIMode {
    // 005D1E10 (slot 5). When idle: action 6 (follow) with the leader as target.
    void Execute(AIBrain* brain);
    // 005D1DE0 (slot 10). Drops the leader when it is the deleted object.
    void OnObjectDeleted(CEngineObject* object);
    // 005D1E80 (slot 12). Saves the leader's reference.
    void Save(AIStream* stream);

    CEngineObject* leader;          // +0x08
};
static_assert(sizeof(FollowMode) == 0x0C);

// Suspection (vtable 0073D1F8, 0x18 bytes): go and look where something was noticed.
struct SuspectionMode : AIMode {
    // 005D1E90 (slot 4). Remembers a non-allied seen object, then takes over the brain's noticed
    // point and level (the level only grows down to the noticed one while the point is the same).
    int Evaluate(AIBrain* brain);
    // 005D1F80 (slot 5). Walk to the noticed point; near it the interest fades.
    void Execute(AIBrain* brain);
    // 005D21A0 (slot 6). Busy; walk to the point; remark 5.
    void Enter(AIBrain* brain);
    // 005D2310 (slot 7). Clears busy; remark 3.
    void Leave(AIBrain* brain);
    // 005D2330 / 005D2360 (slots 11 / 12).
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    Vector3 point;                  // +0x08
    int level;                      // +0x14
};
static_assert(sizeof(SuspectionMode) == 0x18);

// Aggression (vtable 0073D260, 0x18 bytes): fight an enemy.
struct AggressionMode : AIMode {
    // 005D2390 (slot 4). 10000 while there is an enemy to fight (the script's, or the best one
    // in sight), else -1000.
    int Evaluate(AIBrain* brain);
    // 005D2580 (slot 5). Attack the chosen enemy (or cast spell `enemyPart` at it).
    void Execute(AIBrain* brain);
    // 005D26D0 (slot 6). Busy; drops action and target; remark 6.
    void Enter(AIBrain* brain);
    // 005D2710 (slot 7). Forgets the noticed level when the enemy is gone or dead, drops the enemy,
    // clears busy; remark 4.
    void Leave(AIBrain* brain);
    // 005D27A0 (slot 12).
    void Save(AIStream* stream);

    CEngineObject* enemy;           // +0x08
    std::uint8_t fixed;             // +0x0C enemy set by the script (no searching)
    std::uint8_t unknown_0D[3];
    int enemyPart;                  // +0x10 -1: the whole unit
    CEngineObject* chosen;          // +0x14 the enemy chosen this tick
};
static_assert(sizeof(AggressionMode) == 0x18);

// CorpseWatcher (vtable 0073D294, 8 bytes): reports corpses the brain has seen.
struct CorpseWatcherMode : AIMode {
    // 005D27D0 (slot 4). Takes one seen corpse off the brain's list and notes it as noticed
    // (level 800, kind -3). Never takes charge (-1000).
    int Evaluate(AIBrain* brain);
};

// Fear (vtable 0073FF8C, 0x2C bytes): run from an enemy.
struct FearMode : AIMode {
    // 005D2850 (unit_ai_brain.cpp; after operator new). The vtable, distance from 0073FF00, 3, an
    // unset point (x = -1313), +0x24 = 0 and `flag`; +4 and +0x14..+0x20 are left alone. Returns this.
    FearMode* Construct(std::uint8_t flag);
    // unit_ai_mode_lifetime.cpp. 005D2950: the root vtable. 005D2930 (slot 0, RET 4): that, then
    // operator delete when `flags` bit 0 is set; returns this.
    void Destruct();
    FearMode* DeletingDestruct(int flags);
    // 005D3650 (slot 6). Busy; drops action and target.
    void Enter(AIBrain* brain);
    // 005D3680 (slot 7). Clears busy.
    void Leave(AIBrain* brain);
    // 005D3690 / 005D36F0 (slots 11 / 12).
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    float distance;                 // +0x08 (10)
    float unknown_0C;               // +0x0C (3)
    Vector3 unknown_10;             // +0x10 (x = -1313: unset)
    Vector3 unknown_1C;             // +0x1C
    std::uint8_t flag;              // +0x28
    std::uint8_t unknown_29[3];
};
static_assert(sizeof(FearMode) == 0x2C);
static_assert(offsetof(FearMode, flag) == 0x28);

// Player (vtables 0073F0C4 / 0073F0F8, 0x24 bytes): the party's units.
struct PlayerMode : AIMode {
    enum { kStateFollow = 0, kStateCommand = 1, kStateMoveTo = 2 };

    // 005D3760 (unit_ai_mode_lifetime.cpp). Vtable 0073F0F8, no target or leader, point 0, state 0,
    // not running (+4 and the padding untouched); returns this.
    PlayerMode* Construct();

    // 005D4A90 (slot 5). State 0: follow the leader (unless the leader is moving itself) and fight
    // (005D4170); state 1: wait for the commanded action to end; state 2: walk to `point` (staying
    // with the party unless the player controls the unit directly) and fight on the way.
    void Execute(AIBrain* brain);
    // 005D37A0 (slot 10). Drops either reference when it is the deleted object.
    void OnObjectDeleted(CEngineObject* object);
    // 005D4DB0 (slot 12).
    void Save(AIStream* stream);

    // The player's commands (state 1 or 2: the unit does what it was told).
    // 005D39E0. Walk to `to` + `offset` (just `to` when the way there is blocked); forgets the leader
    // unless the player controls the unit directly.
    void CommandWalk(AIBrain* brain, const Vector3* to, int unknown, int run, const Vector3* offset);
    // 005D3AE0. The same in state 2 (keep fighting on the way); when the unit would leave the party
    // it only turns towards the point.
    void CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int run, const Vector3* offset);
    // 005D3C30. Attack `object`.
    void CommandAttack(AIBrain* brain, CEngineObject* object, int how, int unknown);
    // 005D3CF0. Use something on `object` (AIBrain::UseOn).
    void CommandUse(AIBrain* brain, CEngineObject* object, int kind, int a, int b);
    // 005D3D20. Cast spell `spell` at `object`.
    void CommandCast(AIBrain* brain, int spell, CEngineObject* object, int unused);
    // 005D3D90. Cast spell `spell` at `point`.
    void CommandCastAt(AIBrain* brain, int spell, const Vector3* point, int unused);

    // Not rewritten (forwarders).
    void UpdateCombat(AIBrain* brain);                      // 005D4170
    bool StaysWithParty(AIBrain* brain, bool moving);       // 005D4860
    // 005D37E0 (unit_ai_moves.cpp). Can the unit get from `from` to `to`: false when they are within
    // 0.1 (2D), else a path search with the unit moved to `from` for it (range twice the distance).
    bool PathClear(AIBrain* brain, const Vector3* from, const Vector3* to);

    CEngineObject* target;          // +0x08
    CEngineObject* leader;          // +0x0C
    Vector3 point;                  // +0x10 (z: 1.0 while walking there in state 2)
    int state;                      // +0x1C
    std::uint8_t run;               // +0x20
    std::uint8_t unknown_21[3];
};
static_assert(sizeof(PlayerMode) == 0x24);

// A counted reference (CEngineObject refcount at +4, released through vtable slot 4).
struct AIRef {
    // 0052B030. Release and clear.
    void Reset();
    // 0052A8F0. Hold `object` (released first unless it is already held).
    void Assign(CEngineObject* object);
    // 0050E220 / 0050E290. Read / write the object id (-1: none); Load looks it up in `world`.
    void Load(AIStream* stream, void* world);
    void Save(AIStream* stream);

    CEngineObject* object;
};

namespace console {
class GameOStream;
}
// 007C53C4: the stream the AI's perception messages go to.
inline console::GameOStream* AiLog() { return *reinterpret_cast<console::GameOStream**>(0x007C53C4); }

// 005CA050 (ECX = out, EDX = point; unit_ai_brain.cpp). Copies x and y of a point (the AI events'
// and the heard calls' noticed points); returns `out`.
float* __fastcall CopyPoint2(float* out, const float* point);

// A call the perception memory heard (0x18 bytes; CallAllies sends kind 0x1A).
struct AICallRecord {
    Vector3 point;                  // +0x00
    int kind;                       // +0x0C
    int unknown_10;                 // +0x10 0 when added
    char* text;                     // +0x14 CString

    // 00558D00 (unit_ai_brain.cpp). The element destructor: frees the text (CString::~CString).
    void Destruct();
};
static_assert(sizeof(AICallRecord) == 0x18);

// CArray<AICallRecord> (brain + 0x178). Its SetSize stays the instantiation's (005CDD60).
using AICallArray = mfc::CArray<AICallRecord, mfc::RawElements<AICallRecord>>;
void __fastcall CallsSetSizeAt(AICallArray* calls, void*, int size, int growBy);   // 005CDD60
// 005CC9D0 (RET 8; unit_ai_brain.cpp; each tick, from UpdateMemory). Counts every call down;
// one that runs out is dropped after it is heard: with a hostility mask (+0x10) the unit's
// controller takes the mask (logged "hear delayed sound"), else, when the memory's noticed
// level is below 900, the call's point becomes the noticed point (900, kind -3; logged when the
// level rose by more than 100 or the point lies more than sqrt(10) away).
void __fastcall CallsUpdate(AICallArray* calls, void*, CUnit* unit, void* memory);

// An object the memory reported (8 bytes; the object is a counted reference).
struct AIReport {
    CEngineObject* object;
    int what;
};
static_assert(sizeof(AIReport) == 8);

// CArray<AIReport> (brain + 0x164; thiscall on the array, unit_ai_brain.cpp). Its SetSize and
// RemoveAt stay the instantiation's (005CD670 / 005CD800).
using AIReportArray = mfc::CArray<AIReport, mfc::RawElements<AIReport>>;
// 005CC620. Keeps the larger `what` of a listed object, else appends it (with a reference).
void __fastcall ReportsAdd(AIReportArray* reports, void*, CEngineObject* object, int what);
// 005CC560. Whether `object` is listed (only AL is set).
bool __fastcall ReportsContains(AIReportArray* reports, void*, CEngineObject* object);
void __fastcall ReportsSetSizeAt(AIReportArray* reports, void*, int size, int growBy);     // 005CD670
void __fastcall ReportsRemoveAtAt(AIReportArray* reports, void*, int index, int count);    // 005CD800

// One way the unit can attack or help (0x28 bytes): a weapon (part -1) or a spell.
struct AIAttackOption {
    int kind;                       // +0x00 0..3 attacks (2, 3 hit an area), 4..6 helping spells, 7 healing
    int part;                       // +0x04 -1 a weapon, else the spell
    float range;                    // +0x08
    int maxCount;                   // +0x0C cap on the group-size bonus
    float radius;                   // +0x10 area of kinds 2 and 3
    int effect;                     // +0x14 effect type (kinds 4..6), strength (kind 3)
    std::uint16_t damage[8];        // +0x18 measured against the target's protection (00522AE0)
};
static_assert(sizeof(AIAttackOption) == 0x28);

// CArray<AIAttackOption> (0x14 bytes).
struct AIAttackOptions {
    void* vtable;
    AIAttackOption* data;           // +0x04
    int size;                       // +0x08
    int max;                        // +0x0C
    int growBy;                     // +0x10
};
static_assert(sizeof(AIAttackOptions) == 0x14);

using AIObjectList = mfc::CTypedPtrList<CEngineObject>;

// The brain's attention (+0xA4, 0x2C bytes): the unit's attack options and choosing a target
// (unit_ai_targets.cpp; the methods are __thiscall on the attention, the brain passed).
struct AIAttention {
    // 005D0030 (RET 0x18). The best {part, object} to attack or help into `partAndObject`: the
    // current attack scored first, then (without `alliesOnly`) every weapon against every
    // candidate and every spell against the unit and its allies in sight; the best that scores
    // above the current one and passes IsUsable, the order check (with `alliesOnly`, for targets
    // not near the path) and CanReach wins; a rejected one seen once is reported (20).
    void Choose(AIBrain* brain, int* partAndObject, AIObjectList* candidates, const Vector3* from, bool alliesOnly,
                int reachArg);
    // 005CF7F0 (RET 8). False when the part has no option or an area option (kinds 2, 3) would
    // also hit an ally near the target or the unit itself; true without an object.
    bool IsUsable(AIBrain* brain, const int* partAndObject);
    // 005CF9A0 (RET 0x1C; `this` unused). How good `option` is against `candidate` (`group` the
    // others it would hit or help): by kind, minus 25 per unit of distance beyond the range (x10
    // outside kinds 0..4) when it reached `threshold`, minus twice the hits needed for attacks.
    // -1e30 when it cannot apply. Returned unrounded (ST0).
    double Score(const Vector3* from, CUnit* self, AIObjectList* group, CEngineObject* current, CUnit* candidate,
                 float threshold, const AIAttackOption* option);
    // 005CF3C0 (RET 0xC; not rewritten). Whether the unit can get at the target (a path search).
    bool CanReach(AIBrain* brain, const int* partAndObject, int arg);

    // The option of `part`: the weapons first, then the spells; null when there is none.
    const AIAttackOption* FindOption(int part) const;

    AIAttackOptions weapons;        // +0x00
    AIAttackOptions spells;         // +0x14
    float healWeight;               // +0x28 kind 7's score when the target is badly hurt
};
static_assert(sizeof(AIAttention) == 0x2C);

// The AI's eye (the world server's +0x2838, 007B2798; only the leading part): the unit being
// updated's position, eye and facing (unit_ai_sight.cpp; __thiscall on the viewer, the memory
// passed as the brain's +0xD0).
struct AIViewer {
    // 005CAA00 (RET 8; UpdateMemory each brain tick). The viewer moves to the owner's unit, the
    // nearby units are listed again when due (Refresh), units in the visible list it no longer
    // sees are dropped ("lost from sight": an enemy notices 1000 / -2; a corpse is listed as one
    // first, an allied one noticing 800 / -3), then new ones are looked for (See).
    void Update(UnitController* owner, AIBrain* brain);
    // 005CA1D0 (RET 8). Every 8..11 updates (random): the units of the sector cells within the
    // larger of the unit's two sight ranges become the nearby list (+0xF4, counted).
    void Refresh(CUnit* self, AIBrain* brain);
    // 005CA5A0 (RET 8). Nearby units not yet visible (corpses not yet listed) that the unit sees:
    // within sight range, inside the view cone and the line of sight; or with peripheral vision;
    // or sensed alive. Players' units see everything; others only enemies.
    void See(CUnit* self, AIBrain* brain);
    // 005CA410 (RET 4). Raises the eye by the AI map's ground level of its cell and the unit's
    // height parts and sets the facing from the unit's orientation (once per update).
    void Prepare(CUnit* self);

    CAIMap* map;                    // +0x00
    int unknown_04;
    Vector3 eye;                    // +0x08
    Vector3 facing;                 // +0x14
    int cellX;                      // +0x20 AI map cell of the eye
    int cellY;                      // +0x24
    std::uint8_t ready;             // +0x28 Prepare has run
    std::uint8_t unknown_29[3];
};
static_assert(offsetof(AIViewer, facing) == 0x14);
static_assert(offsetof(AIViewer, ready) == 0x28);

// 005D9E10 (__stdcall, RET 8; unit_ai_mode_lifetime.cpp). ConstructElements of `count` attack
// options: zero bytes.
void __stdcall ZeroAttackOptions(AIAttackOption* first, int count);
// 005D9830 (__stdcall, RET 4). CList::GetNext of the AI's pointer lists: the address of the node's
// data slot (node + 8); `position` (a node) moves on to the next.
void* __stdcall ListGetNext(void** position);

// 005CFE20 (__fastcall, unit_ai_targets.cpp). The squared 2D distance, unrounded (ST0).
double __fastcall AIDistance2(const Vector3* a, const Vector3* b);

// 005CD500 (thiscall on a counted object list, RET 4; unit_ai_memory.cpp). Appends `object`
// holding a reference; returns the node.
AIObjectList::Node* __fastcall AddCountedTail(AIObjectList* list, void* edx, CEngineObject* object);

// The brain (controller + 0x980, 0x190 bytes; the controller's voice object follows at +0xB10).
struct AIBrain {
    // Actions (`action`).
    enum {
        kActionNone = 0,
        kActionWalk = 1,
        kActionMove = 2,        // move to `actionPoint`, then the queued looks
        kActionAttack = 3,
        kActionCast = 4,
        kActionUse = 5,
        kActionFollow = 6,
    };
    static constexpr int kNoTime = -10000;          // actionTime: no look in progress
    static constexpr int kLookStarted = -9999;      // actionTime: the first queued look is turning

    // 005D53B0. One AI tick: alarm countdown, decay of alertness, perception update, then every
    // mode's Evaluate; the highest (the current mode gets +100) takes over (Leave / Enter, with a
    // "new motivation" console message), its Execute runs, and the brain carries out its action.
    void Think();

    // 005D5DD0. The mode of type `type` in the mode list, or null.
    AIMode* FindMode(int type);
    // 005D5E30 / 005D5E60. The Patrol / Player mode, or null.
    AIMode* FindPatrolMode();
    AIMode* FindPlayerMode();
    // 005D5EE0. The Player mode's target (+0x0C of the mode), or null.
    CEngineObject* PlayerModeLeader();
    // 005D5E90. The same, or the action target while following (action 6).
    CEngineObject* LeaderOrFollowed();
    // 005D5F20. The Aggression mode's enemy while that mode is in charge, else null.
    CEngineObject* AggressionEnemy();
    // 005D50E0. Is `point` within `radius` of the segment from the previous to the current position
    // (both 2D)? The original sets only AL in one path; callers test AL.
    bool IsNearPath(const Vector3* point, float radius);
    // 005D5080. Sets the owner; on a change both remembered positions become "unset" (-1313, 0, 0).
    // Returns what the original leaves in EAX.
    std::uint32_t SetOwner(UnitController* newOwner);
    // 005D58D0. An object is being deleted: tell every mode, drop it as target or seen object and
    // from the perception memory.
    void OnObjectDeleted(CEngineObject* object);

    // Mode list and state (unit_ai_brain.cpp).
    // 005D5F60. Adds `mode` (a counted reference, RefPtr::Share) at the end of the list after
    // removing the first mode of the same type, or the first exclusive one when `mode` is exclusive
    // (the mode in charge loses its reference when it is the one removed).
    void AddMode(AIMode* mode);
    // 005D6140. Removes every mode of `type` (the mode in charge is left alone).
    void RemoveModesOfType(int type);
    // 005D4DF0. Empties the mode list, drops the mode in charge, resets the near point (+0x44 /
    // +0x48) and the decay.
    void ClearModes();
    // 005D4E80. ClearModes, then no action, memory reset, action point and looks cleared, target
    // and seen object released (the controller's death).
    void Reset();
    // 005D4F70. The state of a new controller's brain: Reset's work plus owner, alert and alarm
    // state, alarm records, positions.
    void Init();
    // 005CB0F0 (thiscall on +0xD0). Empties the perception memory: its four object lists, the noticed
    // point, the report arrays.
    void ResetMemory();
    // The standard mode sets of the script's Guard / Sentry commands: ClearModes, then the guard
    // (radius, wander -1: the default) or sentry mode, Suspection, Aggression, CorpseWatcher and
    // Fear(0); the target is released and the action dropped. 005D6230 / 005D64E0.
    void StandardGuard(const Vector3* point, float radius);
    void StandardSentry(const Vector3* point);
    // New modes handed to AddMode (operator new; a failed allocation adds null).
    void AddGuardMode(const Vector3* point, float radius, float wander);    // 005D6300
    void AddSuspectionMode();                       // 005D63B0 (point and level left unset)
    void AddAggressionMode();                       // 005D63F0
    void AddCorpseWatcherMode();                    // 005D6440
    void AddFearMode(std::uint8_t flag);            // 005D6480 (distance from 0073FF00, 3, unset point)
    void AddSentryMode(const Vector3* point);       // 005D65B0

    // 005D7520. Action 2 (move) after arrival: run the queued looks, each turning then waiting.
    void UpdateMove();
    // 005D76A0. Action 3 (attack): give up when the target is dead, no longer an enemy or the
    // controller stopped attacking it.
    void UpdateAttack();
    // 005D7790. Drop the action and its target.
    void StopAction();
    // 005D7AE0. Action 1: walk to `point`.
    void Walk(const Vector3* point, int run, int unknown);
    // 005D7B30. Action 1: turn towards `direction`.
    void TurnTowards(const Vector3* direction, float time);
    // 005D7DA0. Action 4 (when the spell cannot be cast now): cast `spell` at `point`.
    void CastAt(int spell, const Vector3* point, int run);
    // 005D7E40. Action 6: follow `object`.
    void Follow(CEngineObject* object);
    // 005D7E90. Something was noticed: remember `object` as seen, call allies to it (or to the unit
    // itself when null) unless it is an ally, become alerted in alert mode 1, and remember a unit in
    // the perception memory.
    void SetSeen(CEngineObject* object);
    void Remember(CUnit* self, CEngineObject* object);              // 005CBC80 on +0xD0
    // 005D7B50. Action 3 against `object` (run unless the player's order check says otherwise).
    void AttackTarget(CEngineObject* object);
    // 005D7C60. Action 3 against `object` with the given attack parameters; units of other sides
    // that are not enemies are not attacked unless the unit has a player.
    void AttackWith(CEngineObject* object, int how, int unknown);
    // 005D7CE0. Action 5: use something on `object` (kind 0 a point/object, 1 and 2 units, 0x18A95
    // a special unit use).
    void UseOn(CEngineObject* object, int kind, int a, int b);

    // unit_ai_moves.cpp.
    // 005D6F90. Action 6 (follow `target`): every few ticks walk to where the leader is or will be
    // (a moving leader is met ahead on its way, or passed on the side); a dead or lost leader ends it.
    void UpdateFollow();
    // 005D5B40. Replaces the looks of `order` by one to four random directions (each the first that
    // the AI map sees clear, within 6 tries) around `direction`, each followed by a random wait
    // between a quarter and all of the wander time (40 when negative, else 15 x `wander`). A `far`
    // look (not for busy units nor "unhu*" prototypes) is a single one with a longer wait.
    void PlanLooks(AIMoveOrder* order, const Vector3* direction, float wander, bool far);
    // 005D7F80. A random place within `radius` of `point` to stand at: not too steep, no unit of an
    // allied player near it, and a path there not much longer than the way. After a failure the
    // next calls (up to 10, a third of the distance) fail at once.
    bool FindStandPoint(const Vector3* point, float radius, Vector3* out);
    // 005D5990 (unit_ai_brain.cpp). Every allied unit within +0x38 of the owner's unit hears a call
    // for help (kind 0x1A) at `point`; the message text is formatted only while the console shows
    // filter bit 0.
    void CallAllies(const Vector3* point);
    // 005CFE40 (thiscall on the attention +0xA4 with the brain; unit_ai_attention.cpp). Chooses the
    // best target in sight (005D0030 from the unit's position) and, when it differs from the current
    // attack, attacks it (action 3; the player's order check may lift the flags to -1) or casts
    // spell `part` at it (action 4 when the spell cannot be cast now).
    void LookAround(int unused);
    // 005CF220 (thiscall on +0xA4 with the brain; unit_ai_attention.cpp). The current attack as
    // {part, object}: action 3 {-1, target}, action 4 {spell, target}, the object only when it is a
    // unit; otherwise only the object is cleared.
    void CurrentAttack(int* partAndObject);
    void ChooseTarget(int* partAndObject, void* candidates, const Vector3* from, int a, int b); // 005D0030 on +0xA4
    void CollectEnemies(CUnit* self, void* list);                       // 005CB260 on +0xD0

    // unit_ai_memory.cpp (thiscall on the memory +0xD0 in the original; the modes call them
    // through their addresses).
    // 005CB260 (RET 8). `out` emptied, then the visible units hostile to the unit (its
    // IsHostileTo), alive and not reported, in visible-list order (no references taken).
    void FindEnemies(CUnit* self, AIObjectList* out);
    // 005CB480 (RET 0x10). The same with the visible unit's own IsHostileTo(self); one at or beyond
    // `radius` of `point` (3D; all of them for a radius <= 0) only when it has an aggression mode
    // (not a fixed one without an enemy) whose enemy is none, not a unit, or a unit of a side the
    // unit's side is allied with, and its brain sees an enemy.
    void FindEnemiesNear(CUnit* self, AIObjectList* out, const Vector3* point, float radius);
    // 005CB670 (RET 4). The nearest (3D) visible unit hostile to the unit and alive, or null.
    CUnit* NearestEnemy(CUnit* self);
    // 005CBC80 (RET 8). Forces `object` into sight when it is not visible and attacks the unit or
    // stands at or beyond one of the two sight ranges: a corpse joins +0x12C (and the corpses
    // when allied), a living unit the visible list's head (both counted); each logged.
    void ForceSee(CUnit* self, CEngineObject* object);
    // 005CBE90 (RET 8; the server's OnDamaged). Logs and adds `corpse` to +0x12C (and to the
    // corpses when its side is allied with the unit's), both counted.
    void SeeCorpse(CUnit* self, CEngineObject* corpse);
    // 005CB850 (RET 8; each server tick). The unit's exposure to the living hostile units nearby
    // (+0xF4) into `out` (5 floats, CUnit + 0x4C8): per kind the smallest distance / range ratio,
    // at most 3 (a zero range counts 3). [0] sight (scaled by the line of sight between the eyes,
    // only within range), [4], [1], [2] three other ranges; [3] stays 3.
    void MeasureExposure(UnitController* self, float* out);
    bool IsReported(CEngineObject* object);                             // 005CC560 on +0x164
    void Perceive(UnitController* owner);       // 005CB700 through its address (UpdateMemory)
    // 005CB700 (thiscall on +0xD0; unit_ai_brain.cpp). Ages the reports (one tick off each; an
    // entry below 0 is dropped), updates the heard calls (005CC9D0) and the AI events' view
    // (005CAA00 on 007B2798), then adds the noticed kind to the noticed level. Both callees may
    // set the noticed point, level and kind inline first (900 / -3, 800 / -3, 1000 / -2).
    void UpdateMemory(UnitController* owner);
    // 005CB7F0 (thiscall on +0xD0; unit_ai_brain.cpp). 1 when `other` has the unit's side or player or
    // is in the +0x110 list, else 0.
    int IsEnemy(CUnit* self, CEngineObject* other);
    // 005CBBE0 (thiscall on +0xD0; unit_ai_brain.cpp). Takes over a noticed point of a higher
    // `level`: 1 when the level rose by more than 100 or the point lies more than sqrt(10) away
    // (with the point's own z, not a difference), else 0; 0 when the level is not higher.
    int Notice(const Vector3* point, int level, int kind);
    // 005CC020 (thiscall on +0xD0; unit_ai_brain.cpp). Drops `object` from the four memory lists
    // and the reports (holding a reference meanwhile).
    void Forget(CEngineObject* object);
    void Report(CEngineObject* object, int what);                       // 005CC620 on +0x164
    AIRef* PopCorpse(AIRef* out);                                       // 005D8D50 on +0x148

    UnitController* owner;          // +0x000
    int action;                     // +0x004
    Vector3 actionPoint;            // +0x008
    AILookArray looks;              // +0x014 what to look at after arriving
    int actionTime;                 // +0x028 game time the current look ends (or kNoTime / kLookStarted)
    CEngineObject* target;          // +0x02C
    CEngineObject* seen;            // +0x030
    std::uint8_t alerted;           // +0x034
    std::int8_t alarm;              // +0x035 alarm kind raised when the countdown ends
    std::int8_t unknown_36;
    std::uint8_t unknown_37;
    float unknown_38;               // +0x038 (10.0 from 0073FED8 at Init)
    int alertMode;                  // +0x03C
    int alarmCountdown;             // +0x040
    float unknown_44;               // +0x044 near point (Guard); 100000 when cleared
    float unknown_48;
    float decay;                    // +0x04C alertness decay factor
    Vector3 previousPosition;       // +0x050
    Vector3 position;               // +0x05C
    int standCooldown;              // +0x068 FindStandPoint fails while above 0 (one off per call)
    int followTimer;                // +0x06C UpdateFollow acts once it has counted past 8
    mfc::CTypedPtrList<AIMode> modes;   // +0x070 (elements are counted references)
    AIMode* current;                // +0x08C
    std::uint8_t alarmRecords[0x14];    // +0x090 CArray of 0x4C-byte alarm records (data at +0x94)
    AIAttention attention;          // +0x0A4
    std::uint8_t memory[8];         // +0x0D0 perception memory (005CB700 ...); continues below
    SpiralCache sightCells;         // +0x0D8 the cells around the unit it looks for units in (005CA1D0)
    float noticedX;                 // +0x0E0
    float noticedY;                 // +0x0E4
    int noticedLevel;               // +0x0E8
    int noticedKind;                // +0x0EC
    int nearbyCountdown;            // +0x0F0 updates until the nearby units are listed again (005CA1D0)
    mfc::CTypedPtrList<CEngineObject> unknown_0F4;  // +0x0F4 (elements are counted references)
    mfc::CTypedPtrList<CEngineObject> visible;  // +0x110 units in sight
    mfc::CTypedPtrList<CEngineObject> unknown_12C;  // +0x12C (elements are counted references)
    mfc::CTypedPtrList<CEngineObject> corpses;  // +0x148 (elements are counted references)
    AIReportArray reports;                      // +0x164 objects reported to the alarm code
    AICallArray calls;                          // +0x178 calls heard (005CBBA0)
    std::uint8_t seesEnemy;         // +0x18C
    std::uint8_t unknown_18D[3];
};
static_assert(offsetof(AIBrain, looks) == 0x14);
static_assert(offsetof(AIBrain, actionTime) == 0x28);
static_assert(offsetof(AIBrain, alertMode) == 0x3C);
static_assert(offsetof(AIBrain, decay) == 0x4C);
static_assert(offsetof(AIBrain, previousPosition) == 0x50);
static_assert(offsetof(AIBrain, position) == 0x5C);
static_assert(offsetof(AIBrain, modes) == 0x70);
static_assert(offsetof(AIBrain, current) == 0x8C);
static_assert(offsetof(AIBrain, alarmRecords) == 0x90);
static_assert(offsetof(AIBrain, memory) == 0xD0);
static_assert(offsetof(AIBrain, unknown_0F4) == 0xF4);
static_assert(offsetof(AIBrain, unknown_12C) == 0x12C);
static_assert(offsetof(AIBrain, calls) == 0x178);
static_assert(offsetof(AIBrain, noticedX) == 0xE0);
static_assert(offsetof(AIBrain, visible) == 0x110);
static_assert(offsetof(AIBrain, corpses) == 0x148);
static_assert(offsetof(AIBrain, reports) == 0x164);
static_assert(offsetof(AIBrain, seesEnemy) == 0x18C);
static_assert(sizeof(AIBrain) == 0x190);
static_assert(offsetof(AIBrain, attention) == 0xA4);

inline AIBrain* UnitController::Brain() { return reinterpret_cast<AIBrain*>(brain); }

// The mode constructors the script's UM* commands call after operator new (script_unit_modes.cpp;
// __thiscall, each returns the mode). 004BA1C0 Guard (point, radius, wander: GuardMode::Set),
// 004BA360 Sentry (SentryMode::Set), 004BA450 Follow (the leader referenced), 004BA610 Suspection,
// 004BA710 Aggression (no enemy, part -1), 004BA830 Revenge (Aggression, `fixed` from the argument),
// 004BA920 CorpseWatcher; 004BA1A0 a patrol point's look entry.
GuardMode* __fastcall ScriptGuardModeConstruct(GuardMode* mode, void* edx, const Vector3* point, float radius, float wander);
SentryMode* __fastcall ScriptSentryModeConstruct(SentryMode* mode, void* edx, const Vector3* point);
FollowMode* __fastcall ScriptFollowModeConstruct(FollowMode* mode, void* edx, CEngineObject* leader);
SuspectionMode* __fastcall ScriptSuspectionModeConstruct(SuspectionMode* mode);
AggressionMode* __fastcall ScriptAggressionModeConstruct(AggressionMode* mode);
AggressionMode* __fastcall ScriptRevengeModeConstruct(AggressionMode* mode, void* edx, int fixed);
CorpseWatcherMode* __fastcall ScriptCorpseWatcherModeConstruct(CorpseWatcherMode* mode);
AILook* __fastcall ScriptAILookConstruct(AILook* look, void* edx, float angle, float wait, float time, int stopFirst);
