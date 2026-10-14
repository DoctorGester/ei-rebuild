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
#include "map_spec.h"
#include "maths.h"
#include "unit.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct AIBrain;
struct AIOrder;

using AIObjectList = mfc::CTypedPtrList<CEngineObject>;

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
    void Turn(const Vector3* direction, float time);        // 005546B0
    void Say(int what);                                     // 005AEA60 on this + 0xB10 (see Voice)
    void MoveTo(const Vector3* point, int run, int unused, float time);    // 00554740
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
    // 00670760 (RET 8; player_server_handlers.cpp). Clear, then a use (6) of `object` (referenced)
    // with `what`.
    void MakeUse(CEngineObject* object, int what);
    // 006707D0 (RET 4). Where the order aims: the object's AimPoint (slot 53), or `point` without
    // an object. Returns `out`.
    Vector3* AimPoint(Vector3* out) const;

    int kind;                       // +0x00
    int what;                       // +0x04 6 for an attack, the spell for a cast
    CEngineObject* object;          // +0x08 counted
    Vector3 point;                  // +0x0C left uninitialised by the AI's orders
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
// 005D9010 / 005D91B0 (fastcall, plain RET; unit_ai_save.cpp). The looks from / to a save: the
// count, then angle, time and wait of each and (files from version 0x44) the stop flag.
void __fastcall LoadLooks(AILookArray* looks, AIStream* stream);
void __fastcall SaveLooks(const AILookArray* looks, AIStream* stream);
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
    // 005D7970 (RET 4; unit_ai_looks.cpp): operator=. The point, then the looks (the array's Copy
    // inlined); returns this. Only the original fear mode 005D30A0 calls it (ours assigns the
    // parts through the look array's address).
    AIMoveOrder* Assign(const AIMoveOrder* other);

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
// Tamed (type 8): a Player mode (PlayerMode::Construct) given this vtable (00552150: GetType 8).
inline void* const kTamedVtable = reinterpret_cast<void*>(0x0073F0C4);
// 00552090 / 00552140 (slot 4, RET 4): the player mode claims 10, the tamed one 100000000.
// 005520A0 / 00552150 (slot 8): their types 6 and 8 (unit_ai_player_mode_slots.cpp).
int __fastcall PlayerPriority(AIMode* self, void*, AIBrain* brain);
int __fastcall TamedPriority(AIMode* self, void*, AIBrain* brain);
int __fastcall PlayerType(AIMode* self);
int __fastcall TamedType(AIMode* self);
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
    // The element assignment of the point array's Add / SetAtGrow (unit_ai_looks.cpp): the
    // position, then the looks' SetSize to the source's count and each look copied.
    PatrolPoint& operator=(const PatrolPoint& other);

    Vector3 position;               // +0x00
    AILookArray looks;              // +0x0C
};
static_assert(sizeof(PatrolPoint) == 0x20);

// The point array's ConstructElements (004CDE30: zeroed, each look array given vtable 0073D4B4) and
// DestructElements (004C9AF0 on each: the look array's base vtable, its data freed);
// unit_ai_looks.cpp.
struct PatrolPointElements {
    static void Construct(PatrolPoint* points, int count);
    static void Destruct(PatrolPoint* points, int count);
};

// CArray<PatrolPoint> (PatrolMode + 8, vtables 0073FF74 / 0073FF80; unit_ai_mode_lifetime.cpp).
// Add 005D93B0 and SetAtGrow 005D9F30 are registered on the template (unit_ai_looks.cpp); its
// SetSize stays 004C9970 (PatrolPointArraySetSizeOriginal).
using PatrolPointArray = mfc::CArray<PatrolPoint, PatrolPointElements>;
// 005D8CF0. The base vtable, every point's looks freed, then the buffer (pointer and count kept).
void __fastcall PatrolPointsDestruct(PatrolPointArray* points);
// 005D8CD0 (RET 4). Destruct, then operator delete when `flags` bit 0 is set; returns `points`.
PatrolPointArray* __fastcall PatrolPointsDeletingDestruct(PatrolPointArray* points, void*, int flags);
// 005D9E40 / 005D9590 (RET 8; unit_ai_mode_lifetime.cpp). GetAt by value: point `index` copied into
// `out` (a new look array); returns `out`.
PatrolPoint* __fastcall PatrolPointCopyAt(const PatrolPointArray* points, void*, PatrolPoint* out, int index);

// Patrol (vtable 0073FF40, 0x20 bytes): walk a list of points.
struct PatrolMode : AIMode {
    // 005D16B0 (slot 4). 50; without points: FatalError "Unit %i has no patrol points, switching to
    // guard", a point at the unit's position, and -1000.
    int Evaluate(AIBrain* brain);
    // 005D1700 (slot 5). When idle: on to the next point and its looks.
    void Execute(AIBrain* brain);
    // 005D1690. The last point, or null.
    PatrolPoint* LastPoint();
    // 005D1930 / 005D1A40 (slots 11 / 12). The point count (the points' SetSize 004C9970), per point
    // its position, look count (the looks' SetSize 005D8EE0) and per look angle, time, wait and
    // (from version 0x44 when loading) the stop flag byte; then `current`.
    void Load(AIStream* stream);
    void Save(AIStream* stream);
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
    // 005D1140 (slot 2): Destruct, then Construct (the count at +4 kept). 005D0F90 (slot 3): a new
    // mode (0x20 bytes) with copies of the points and `current`; null when the allocation fails.
    void Rebuild();
    PatrolMode* Clone();

    PatrolPointArray points;        // +0x08 (vtables 0073FF74 / 0073FF80)
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
    // 005D1E60 / 005D1E80 (slots 11 / 12). Loads / saves the leader's reference.
    void Load(AIStream* stream);
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
    // 005D2770 / 005D27A0 (slots 11 / 12). The enemy's reference and `fixed`.
    void Load(AIStream* stream);
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
    // 005D2880 (slot 3; unit_ai_mode_lifetime.cpp). A new mode with this one's fields (references
    // 0; the abstract base's vtable stands while they are copied); null when the allocation fails.
    FearMode* Clone();
    // 005D28F0 (slot 2; unit_ai_mode_lifetime.cpp). Destroyed and built again in place without
    // the flag: Construct(0)'s stores after the root vtable (the count and +0x14 .. +0x20 stay).
    // EAX is 0 on return.
    int Reset();
    // 005D2960 (slot 4; unit_ai_fear.cpp). Remembers a seen object (notice 1000 / -2; `distance`
    // grows to 1.5 times its distance) and calms `spread` towards 3. 15000 when a ranged unit (size
    // class below 6, no spell in slot 0, not slowed, load factor above 0.6) has its nearest enemy
    // within [Logic] RangedAttackDist (RangedRunDist while aggressive) and is walking on command or
    // finds a way back 5 from it (a random point within 3, path search, the order check from the
    // path's end, which must lie within 1; then kept in `fleePoint`, z 1). 3000 while it sees an
    // enemy; 350 in shelter (a kind 3 occluder at the unit, a small or non-0 size class unit, or at
    // the last shelter). With `flag` the nearest player unit in sight inside `distance` is noticed.
    // Then noticedLevel / 6 when it is above 50 and the mode is in charge or the noticed point is
    // within `distance`; else `distance` moves a 31st back to the default and -1000.
    int Evaluate(AIBrain* brain);
    // 005D30A0 (slot 5; unit_ai_fear.cpp). Runs from the nearest enemy (calling allies to it), else
    // stays in shelter, else from the noticed point (forgotten unless `flag` and above 500): within
    // `distance`, and not walking on command, to `fleePoint` when one is pending, else 5 away plus
    // a random offset of at most `spread` (which first drops to 2.5 x `distance`), when a path is
    // found (action 1); `spread` doubles. Without an action: attack the best target in sight
    // (allies only, 005D0030), else stand and look away from the point.
    void Execute(AIBrain* brain);
    // 005D3650 (slot 6). Busy; drops action and target.
    void Enter(AIBrain* brain);
    // 005D3680 (slot 7). Clears busy.
    void Leave(AIBrain* brain);
    // 005D3690 / 005D36F0 (slots 11 / 12).
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    float distance;                 // +0x08 how near danger counts (10, FearDefaultDistance)
    float spread;                   // +0x0C random spread of a flight point (3)
    Vector3 shelter;                // +0x10 the last place with a kind 3 occluder (x = -1313: unset)
    Vector3 fleePoint;              // +0x1C a checked flight point (z 1.0 while pending, else 0)
    std::uint8_t flag;              // +0x28
    std::uint8_t unknown_29[3];
};
static_assert(sizeof(FearMode) == 0x2C);
static_assert(offsetof(FearMode, flag) == 0x28);
static_assert(offsetof(FearMode, fleePoint) == 0x1C);
// 0073FF00: a new Fear mode's distance, which `distance` returns to.
inline float FearDefaultDistance() { return *reinterpret_cast<const float*>(0x0073FF00); }

// Player (vtables 0073F0C4 / 0073F0F8, 0x24 bytes): the party's units.
struct PlayerMode : AIMode {
    enum { kStateFollow = 0, kStateCommand = 1, kStateMoveTo = 2 };

    // 005D3760 (unit_ai_mode_lifetime.cpp). Vtable 0073F0F8, no target or leader, point 0, state 0,
    // not running (+4 and the padding untouched); returns this.
    PlayerMode* Construct();
    // unit_ai_player_mode_slots.cpp (A-U6). The tamed vtable 0073F0C4 is the player's with its own
    // slots 0, 2, 3, 4 and 8. Slot 3 (Clone) 00551F50 / 00551E10: a new mode (refs 0, written
    // before the allocation is tested) holding the same target and leader (one more reference
    // each) with the same point, state and run. Slot 2 (OnShutdown) 00552000 / 00551EC0: Destruct,
    // then Construct in place (the tamed one puts its vtable back); returns this.
    // Destruct 005520D0 / 00552180 (identical): releases the leader, then the target, root vtable.
    // Slot 0 005520B0 / 00552160 (RET 4): Destruct, operator delete when `flags` bit 0 is set.
    PlayerMode* ClonePlayer();
    PlayerMode* CloneTamed();
    PlayerMode* ResetPlayer();
    PlayerMode* ResetTamed();
    void Destruct();
    PlayerMode* DeletingDestruct(int flags);

    // 005D4A90 (slot 5). State 0: follow the leader (unless the leader is moving itself) and fight
    // (005D4170); state 1: wait for the commanded action to end; state 2: walk to `point` (staying
    // with the party unless the player controls the unit directly) and fight on the way.
    void Execute(AIBrain* brain);
    // 005D37A0 (slot 10). Drops either reference when it is the deleted object.
    void OnObjectDeleted(CEngineObject* object);
    // 005D4CD0 (slot 11 of both vtables; unit_ai_modes.cpp, A-W14). The target's and the leader's
    // references, the point and the state. Saves of versions 0x46 .. 0x5D hold a group of objects
    // after them (ScriptGroup::Load), which is read and dropped.
    void Load(AIStream* stream);
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

    // 005D3DE0 (RET 8). Follow `leader` (counted): none when it is the unit itself. When the chain
    // of leaders from `leader` (each unit's first Player mode) comes back to this unit, this unit's
    // own Player mode stops following first. A walk in progress (action 1) is dropped (state 0).
    void SetLeader(AIBrain* brain, CEngineObject* leader);

    // unit_ai_player.cpp.
    // 005D4170 (RET 4). Only for units whose +0x438 is 1: the mode's target when it can be reached
    // from here (attacked at once); else the best target (005D0030, no allies) among the enemies
    // near `point` (within [Logic] SwarmRadius in state 2, anywhere otherwise, the point then
    // zeroed) that are no farther than 1.1 x their own sight or hearing, plus what the party is
    // fighting. When there is one, the party's other followers that are not attacking or casting
    // stop (unless the player controls the unit directly), the unit attacks it unless it already
    // does, and the mode's target is dropped.
    void UpdateCombat(AIBrain* brain);
    // 005D3F50 (RET 8). Adds to `targets` (once each) the units that party members within [Logic]
    // PlayerCallForHelp are attacking (action 3), unless they belong to the same player.
    void AddPartyTargets(AIBrain* brain, AIObjectList* targets);
    // 005D4600 (RET 8). `party` emptied, then the unit, the chain of leaders above it (Player
    // mode's leader, or the followed unit), and repeatedly every unit of its player whose leader is
    // listed (the player's units in hash order, up to the first null one).
    void CollectParty(AIBrain* brain, AIObjectList* party);
    // 005D4860 (RET 8; only AL). False when a unit of the player whose (first) Player mode follows
    // this unit is farther than [Logic] FollowMaxDist x (2 while `moving`, else 1.4) + 1 (3D).
    bool StaysWithParty(AIBrain* brain, bool moving);
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

// The brain's corpse list (+0x148; its elements are counted references). 005D8D50 (RET 4,
// unit_ai.cpp; CorpseWatcherMode::Evaluate through AIBrain::PopCorpse): RemoveHead, the head's
// object handed to `out` with a reference (the list's own reference released). Returns `out`.
using CorpseList = mfc::CTypedPtrList<CEngineObject>;
AIRef* __fastcall CorpsesPopHead(CorpseList* list, void*, AIRef* out);

class CSpell;
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
    // The element assignment of the array's Add (unit_ai_brain.cpp): the point, kind and +0x10,
    // then the text (CString::operator=).
    AICallRecord& operator=(const AICallRecord& other);
};
static_assert(sizeof(AICallRecord) == 0x18);

// The calls' ConstructElements (00558D60: zeroed, each text the empty CString) and
// DestructElements (00558D00 each); unit_ai_brain.cpp.
struct AICallElements {
    static void Construct(AICallRecord* calls, int count);
    static void Destruct(AICallRecord* calls, int count);
};

// CArray<AICallRecord> (brain + 0x178). SetSize 005CDD60 and Add 005CD880 are registered on the
// template (unit_ai_brain.cpp; callers reach SetSize through CallsSetSizeAt, its address).
using AICallArray = mfc::CArray<AICallRecord, AICallElements>;

// A unit's alarm / assembly record: the map spec's behaviour record (0x4C bytes, map_spec.h; read
// and written by 0045D610 / 0045D760). CArray at brain + 0x90: elements built by 00558E10 (zeroed,
// then 0045D5B0 each) and destroyed by 0046C290; its SetSize 005D9840 is registered on the template
// (unit_ai_alarm.cpp).
using AIAlarmRecord = mapspec::Behaviour;
struct AIAlarmRecordElements {
    static void Construct(AIAlarmRecord* records, int count);
    static void Destruct(AIAlarmRecord* records, int count);
};
using AIAlarmRecordArray = mfc::CArray<AIAlarmRecord, AIAlarmRecordElements>;
void __fastcall CallsSetSizeAt(AICallArray* calls, void*, int size, int growBy);   // 005CDD60
// 005CC9D0 (RET 8; unit_ai_brain.cpp; each tick, from UpdateMemory). Counts every call down;
// one that runs out is dropped after it is heard: with a hostility mask (+0x10) the unit's
// controller takes the mask (logged "hear delayed sound"), else, when the memory's noticed
// level is below 900, the call's point becomes the noticed point (900, kind -3; logged when the
// level rose by more than 100 or the point lies more than sqrt(10) away).
void __fastcall CallsUpdate(AICallArray* calls, void*, CUnit* unit, void* memory);
// 005CCD40 (RET 0xC; unit_ai_hearing.cpp; the memory's HearCall 005CBBA0 and AIViewer::Noise
// inline it). The calls without a mask whose kind is `kind` or above are dropped, then {point,
// kind, 0, text} is appended.
void __fastcall CallsHear(AICallArray* calls, void*, const Vector3* point, int kind, const char* text);
// 005CCEA0 (RET 0xC; the memory's 005CBBC0). Appends {point not set, kind, mask, text}: when it
// runs out (CallsUpdate) the unit's controller takes the hostility mask.
void __fastcall CallsAddClaim(AICallArray* calls, void*, std::uint32_t mask, int kind, const char* text);
// 005CCBF0 (plain RET). Drops the calls that carry a mask.
void __fastcall CallsRemoveClaims(AICallArray* calls);

// An object the memory reported (8 bytes; the object is a counted reference).
struct AIReport {
    CEngineObject* object;
    int what;
};
static_assert(sizeof(AIReport) == 8);

// The reports' ConstructElements (00558D10: zeroed, each object null) and DestructElements
// (00558CE0 each: the object released, null); unit_ai_brain.cpp.
struct AIReportElements {
    static void __stdcall Construct(AIReport* reports, int count);   // 00558D10 (RET 8)
    static void Destruct(AIReport* reports, int count);
};

// CArray<AIReport> (brain + 0x164; thiscall on the array, unit_ai_brain.cpp). SetSize 005CD670 and
// RemoveAt 005CD800 are registered on the template (A-W12; callers still reach them through
// ReportsSetSizeAt / ReportsRemoveAtAt, their addresses).
using AIReportArray = mfc::CArray<AIReport, AIReportElements>;
// 005CC620. Keeps the larger `what` of a listed object, else appends it (with a reference).
void __fastcall ReportsAdd(AIReportArray* reports, void*, CEngineObject* object, int what);
// 005CC560. Whether `object` is listed (only AL is set).
bool __fastcall ReportsContains(AIReportArray* reports, void*, CEngineObject* object);
void __fastcall ReportsSetSizeAt(AIReportArray* reports, void*, int size, int growBy);     // 005CD670
void __fastcall ReportsRemoveAtAt(AIReportArray* reports, void*, int index, int count);    // 005CD800

// One way the unit can attack or help (0x28 bytes): a weapon (part -1) or a spell.
struct AIAttackOption {
    // 005CE9B0 (ST0; unit_ai_options.cpp). How much the option counts: kind 0 its largest damage,
    // 1..3 that * 1.3, 4..6 1, 7 10, other kinds 0.
    double Weight() const;
    // 005CE860 / 005CE920 (RET 4; unit_ai_options.cpp). The option from / to a save. Before
    // version 0x5A only the part, a dropped dword and the damage were saved: the kind becomes
    // part >= 0, the range 1, the rest 0.
    void Load(AIStream* stream);
    void Save(AIStream* stream);

    int kind;                       // +0x00 0..3 attacks (2, 3 hit an area), 4..6 helping spells, 7 healing
    int part;                       // +0x04 -1 a weapon, else the spell
    float range;                    // +0x08
    int maxCount;                   // +0x0C cap on the group-size bonus
    float radius;                   // +0x10 area of kinds 2 and 3
    int effect;                     // +0x14 effect type (kinds 4..6), strength (kind 3)
    std::uint16_t damage[8];        // +0x18 measured against the target's protection (00522AE0)
};
static_assert(sizeof(AIAttackOption) == 0x28);

// CArray<AIAttackOption> (0x14 bytes; zero-filled elements). Its SetSize 005D9250 is registered on
// the template (unit_ai_options.cpp).
using AIAttackOptions = mfc::CArray<AIAttackOption>;
static_assert(sizeof(AIAttackOptions) == 0x14);


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
    // 005CF3C0 (RET 0xC; only AL). Whether the unit may attack / cast at the target: false for a
    // spell aimed at no object against a large unit of size class 0; else the order is checked
    // (0054C960) from the unit's place when `fromHere` (low byte) is set or the target is the
    // unit itself, otherwise from the end of a path search to the target (range ScaledValue of the
    // distance, the target lifted off the sector map meanwhile), or the unit's place without one.
    bool CanReach(AIBrain* brain, const int* partAndObject, int fromHere);

    // The option of `part`: the weapons first, then the spells; null when there is none.
    const AIAttackOption* FindOption(int part) const;

    // unit_ai_options.cpp:
    // 005CEED0 (RET 4). Lists the unit's options: its weapon (unless its natural weapon is 0x10,
    // "spells only") and attack spells (kinds 1..4) into `weapons`, helping spells (5..7) into
    // `spells`; a unit whose id is a text key gets only the weapon. Then `healWeight` is the
    // largest Weight of the `weapons` options.
    void Build(CUnit* unit);
    // 005CEA30 (RET 0x1C; `this` unused). Fills `option`'s leading fields.
    void InitOption(AIAttackOption* option, int kind, int part, float range, int maxCount, float radius, int effect);
    // 005CEA60 / 005CEBD0 (RET 0x18). Appends an option to `weapons` / `spells`; returns its damage.
    std::uint16_t* AddWeaponOption(int kind, int part, float range, int maxCount, float radius, int effect);
    std::uint16_t* AddSpellOption(int kind, int part, float range, int maxCount, float radius, int effect);
    // 005CEDC0 (RET 4; `this` unused). The option kind of a spell by its type (high word of +0x18):
    // 1..7, 8 for none; 0 (EAX = the null pointer) without a spell.
    int SpellKind(const CSpell* spell);

    AIAttackOptions weapons;        // +0x00
    AIAttackOptions spells;         // +0x14
    float healWeight;               // +0x28 the largest Weight of the weapons (Build); kind 7 scores with it
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
    // 005C98C0 (RET 0x10; unit_ai_hearing.cpp; attacks, skill uses, explosions). A noise of
    // `loudness` made by `source`'s unit at `at` (null: the unit's position): every unit within 50
    // of the point (the sector map) that is hostile to the source's unit, whose noticed level is
    // below 900 and whose hearing (moveFactors[1] x valuesB0[3]) times the loudness reaches past
    // the distance hears a call of `kind` at the source unit's position (CallsHear; the text
    // "Unit %i heard unit %i from %fm distance" only with console filter bit 0).
    void Noise(UnitController* source, float loudness, int kind, const Vector3* at);
    // 005C9770 (RET 8; unit_ai_hearing.cpp, A-W14; the controller's update every few frames). The
    // noise of a moving unit: Noise of kind 0x1A at the unit, as loud as moveFactors[0] x
    // valuesE0[3] x the noise of the ground at `position` (the AI map's terrain type). Returns 0.
    int StepNoise(const Vector3* position, UnitController* source);

    CAIMap* map;                    // +0x00
    int tick;                       // +0x04 the world's event table: its tick (AIEventTable::Advance)
    Vector3 eye;                    // +0x08
    Vector3 facing;                 // +0x14
    int cellX;                      // +0x20 AI map cell of the eye
    int cellY;                      // +0x24
    std::uint8_t ready;             // +0x28 Prepare has run
    std::uint8_t unknown_29[3];
};
static_assert(offsetof(AIViewer, facing) == 0x14);

// 005CD210 (fastcall, RET 4; unit_ai_sight.cpp; the show-visibility effect's tick 0067C840). The
// cells `seer` sees as `observer` would be seen, into `grid` (cleared to one cell first): within
// sight (the two units' sight values, at least 0) inside the cone, up to the observer's mode
// height; within hearing all around; within the peripheral range in the front half.
void __fastcall MarkSightCells(CUnit* seer, CUnit* observer, VisibilityGrid* grid);
static_assert(offsetof(AIViewer, ready) == 0x28);

// An AI event (0x30 bytes): filed in its map cell's list for its side and in the time wheel's slot
// of the tick it expires. Records come from the pool CList at 007C43A8 (AIEventPool) and go back to
// the free list at 007C53F0 (chained through `next`).
struct AIEvent {
    float x, y;                     // +0x00 where; its cell is (x / 8, y / 8) rounded
    std::uint32_t unknown_08[2];    // +0x08 (saved)
    int expiry;                     // +0x10 the tick it goes; wheel slot (expiry - 1) & 0x3FF
    std::uint32_t unknown_14[2];    // +0x14 (saved)
    int sourceId;                   // +0x1C the unit that made it
    int side;                       // +0x20 its low 4 bits: the side list it is filed in
    AIEvent* next;                  // +0x24 the cell's list for the side; the free list's link
    AIEvent* prev;                  // +0x28 null for the cell's first
    AIEvent* nextInSlot;            // +0x2C the wheel slot's list
};
static_assert(sizeof(AIEvent) == 0x30);

// A map cell of the event grid: the newest event of each side (lists through AIEvent::next).
struct AIEventCell {
    AIEvent* bySide[16];
};

class CUnitServer;   // combat.h
class CStream;       // core_streams.h

// The world's AI events (CWorldServer + 0x2838; its viewer part makes the noises): a time wheel of
// 1024 ticks x 16 sides and a grid of 8 x 8 cells (rows of width cells). Its tick is the viewer's
// `tick` (Advance sets it).
struct AIEventTable : AIViewer {
    // 005C9530 (RET 4; world_tick.cpp's Tick). The wheel moves to `tick`: every event of that
    // tick's 16 slots leaves its cell list and goes to the free list; the slots empty. A first
    // event whose cell is outside the grid faults (the original reads a null cell).
    void Advance(int tick);
    // 005C97B0 (RET 4; CUnitServer::OnDamaged on a death). Every event made by `controller`'s unit
    // in the wheel column of its side (unit side & 15) is dropped as Advance drops them.
    void ForgetUnit(CUnitServer* controller);
    // 005CAE30 / 005CAFA0 (RET 4; the save). Load: while a byte 1 precedes, an event (its fields
    // as Save writes them) filed in its cell and wheel slot, or freed when outside the grid. Save:
    // each cell's events by side (byte 1, x / y, +0x08, expiry, +0x14, source, side), then byte 0.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // 005B3F40 (the constructor; the AI map's 005B3C30). A 1 x 1 grid, then Clear. Returns this.
    AIEventTable* Construct();
    // 005C9250. Tick -1, the wheel emptied (its events are not freed), the grid back to 1 x 1
    // (reallocated unless it is) and its cell emptied.
    void Clear();
    // 005C91F0 (the destructor). Every event of the wheel goes to the free list; the grid is freed.
    void Destroy();
    // 005C9310 (RET 0xC; CAIMap::Load, 005B43C0; patched by Rebirth, so vanilla only; A-W14). The
    // table for a map of sizeX x sizeY: Clear, the map kept, the grid 1 - trunc(size * -0.125)
    // cells each way (reallocated unless it has that size) and emptied. Then the 4096 bytes of
    // "normal.dat" in the text resources' "aiinfo.res" are copied to 007C43C4. Returns 1; a
    // missing archive or file is a fatal error, whose result is returned.
    int Setup(CAIMap* aiMap, float sizeX, float sizeY);

    // The cell of `event`, or null outside the grid.
    AIEventCell* CellOf(const AIEvent* event) const;
    // Takes `event` out of column `side` of its cell list and puts it on the free list.
    void Free(AIEvent* event, int side);

    AIEvent* slots[0x400][16];      // +0x02C by tick & 0x3FF, then side
    AIEventCell* cells;             // +0x1002C the grid's block
    AIEventCell** rows;             // +0x10030 the grid rows (each `width` cells)
    int width;                      // +0x10034
    int height;                     // +0x10038
};
static_assert(offsetof(AIEventTable, slots) == 0x2C);
static_assert(offsetof(AIEventTable, rows) == 0x10030);
static_assert(sizeof(AIEventTable) == 0x1003C);

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
    // combat_lifetime.cpp (A-U7). 00545990. The constructor (CUnitServer's, on +0x980): empty
    // containers with their vtables, the move order cleared (00545B20), both sight caches
    // {0, -1}, then Init. Returns this.
    AIBrain* Construct();
    // 005457E0 (reached only from the unwind funclets of the controller's constructor and
    // destructor). Init, the memory destroyed (as 00545CB0), then the attention arrays, alarm
    // records, the mode in charge, the mode list, target / seen and the looks.
    void Destruct();
    // 00545CB0 (thiscall on +0xD0; 00546230 calls it). The perception memory destroyed: calls,
    // reports, then the four object lists (references released, blocks freed).
    void DestructMemory();
    // The action point and its looks as one move order (00545B20 clears them).
    AIMoveOrder* MoveOrder() { return reinterpret_cast<AIMoveOrder*>(&actionPoint); }
    // 005CB0F0 (thiscall on +0xD0). Empties the perception memory: its four object lists, the noticed
    // point, the report arrays.
    void ResetMemory();
    // 005D8310 / 005D8860 (RET 4; unit_ai_brain.cpp). The brain from / to a save: the action, its
    // point, looks and time, target and seen object, the scalars, the modes (count, the index of
    // the one in charge, then each mode's type and its own data), the perception memory, the alarm
    // records, and (by file version) the attack options. Load empties the mode list first, creates
    // each mode by type (stopping at an unknown type, 9 or above 10) and maps a call radius equal
    // to [Logic] CallForHelpRadiusDefault to CallForHelpRadius.
    void Load(AIStream* stream);
    void Save(AIStream* stream);
    // 005CC1E0 / 005CC350 (thiscall on +0xD0, RET 4; unit_ai_save.cpp). The perception memory:
    // the four object lists (appended to on load), the noticed point, level and countdown, the
    // reports, the heard calls (point, kind, text, +0x10) and the noticed kind (-1 until read).
    void LoadMemory(AIStream* stream);
    void SaveMemory(AIStream* stream);
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
    // unit_ai_alarm.cpp. 005D6630 (RET 8). The modes of behaviour `record`: the list emptied, the
    // call radius (the default one becomes [Logic] CallForHelpRadius) and alert mode taken; type 1
    // guards the record's point, 5 the point of world alarm `slot`, 3 a sentry there, 2 patrols
    // (each patrol's start on its half-cell's middle, its waypoints as looks from there; without
    // `loop` the points are walked back too), 4 the party's Player mode. Except for types 0 and 4
    // the reaction (`alarm`) adds Aggression (0) or Revenge (1) with Suspection, CorpseWatcher and
    // Fear(0); 2 CorpseWatcher and Fear(0); 3 CorpseWatcher and Fear(1). Then the alarm (kind,
    // `slot`, countdown at least 1, not alerted), no action or target, the attack options rebuilt,
    // and with flag bit 3 the unit joins the world's update map.
    void ApplyBehaviour(const AIAlarmRecord* record, int slot);
    // 005D6D20 (RET 8; the .mob loader 00481660). The records copied into `alarmRecords`, then the
    // first applied (slot 0).
    void SetAlarmRecords(const AIAlarmRecord* records, int count);
    // 005D5940 (RET 4; the world's RaiseAlarm 00593FC0). The world alarm `slot` was raised: the
    // behaviour record of that slot is applied (the first always, the others only with their
    // +0x38 set) and the owner raises its own alarm to 7. Nothing for a slot without a record.
    void HearAlarm(int slot);
    // 005D7C30 (the script's ResetTarget). The perception memory emptied, then the behaviour
    // record of the slot last applied (unknown_36) applied again.
    void ResetBehaviour();

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
    // 005CF170 (thiscall on +0xA4 with the brain, RET 8; unit_ai_fear.cpp). Takes up {part, object}
    // unless there is no object or it is the current attack: part -1 AttackTarget, else cast it
    // (action 4 when the spell cannot be cast now; target and actionTime = the spell).
    void ApplyAttack(const int* partAndObject);
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
    std::int8_t unknown_36;         // +0x036 the world alarm slot of the applied record (ApplyBehaviour)
    std::uint8_t unknown_37;        // +0x037 always updated (the record's flag bit 3)
    float callRadius;               // +0x038 allies within it hear a call for help (10.0 from 0073FED8 at Init)
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
    AIAlarmRecordArray alarmRecords;    // +0x090
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

// The script-made modes' own slots, 004BA250-004BA9B0 (script_unit_modes.cpp). Clone (slot 3): a
// new mode (operator new, refs and flags 0) with the fields copied (Follow / Aggression reference
// their leader / enemy; Aggression's `chosen` is copied without one); null when the allocation
// fails. Reset (slot 2, OnShutdown): the references dropped and the fields back to what the
// constructor sets (EAX not set). Destruct: the references dropped, the root vtable. Slot 0:
// Destruct, then operator delete when `flags` bit 0 is set; returns the mode. The plain modes'
// destructor bodies only store the root vtable (aimode::DestructToRoot). Slots of 3 bytes (RET 4,
// MOV AL,1, XOR EAX,EAX ...) cannot be hooked: 004BA220 / 004BA230 / 004BA240 / 004BA2B0 / 004BA2C0
// / 004BA2F0 / 004BA620 / 004BA970.
namespace aimode {
// 004BA320 / 004BA330 / 004BA440 / 004BA6A0 / 004BA700 / 004BA9B0: the root vtable.
void __fastcall DestructToRoot(AIMode* mode);
// 004BA340 (abstract base 0073D15C) / 004BA680 (0073D22C), slot 0 (RET 4).
AIMode* __fastcall DeletingDestructBase(AIMode* mode, void*, int flags);
// Slot 8 (GetType): 004BA410 Sentry 2, 004BA580 Follow 10, 004BA6D0 Suspection 3, 004BA860
// Aggression 4, 004BA980 CorpseWatcher 5.
int __fastcall SentryType(AIMode* mode);
int __fastcall FollowType(AIMode* mode);
int __fastcall SuspectionType(AIMode* mode);
int __fastcall AggressionType(AIMode* mode);
int __fastcall CorpseWatcherType(AIMode* mode);
} // namespace aimode
GuardMode* __fastcall GuardClone(const GuardMode* mode);                                // 004BA250
void __fastcall GuardReset(GuardMode* mode);                                            // 004BA2D0
GuardMode* __fastcall GuardDeletingDestruct(GuardMode* mode, void*, int flags);         // 004BA300
SentryMode* __fastcall SentryClone(const SentryMode* mode);                             // 004BA3B0
void __fastcall SentryReset(SentryMode* mode);                                          // 004BA3F0
SentryMode* __fastcall SentryDeletingDestruct(SentryMode* mode, void*, int flags);      // 004BA420
FollowMode* __fastcall FollowClone(const FollowMode* mode);                             // 004BA4A0
void __fastcall FollowReset(FollowMode* mode);                                          // 004BA510
FollowMode* __fastcall FollowDeletingDestruct(FollowMode* mode, void*, int flags);      // 004BA590
void __fastcall FollowDestruct(FollowMode* mode);                                       // 004BA5B0
SuspectionMode* __fastcall SuspectionClone(const SuspectionMode* mode);                 // 004BA630
void __fastcall SuspectionReset(SuspectionMode* mode);                                  // 004BA6B0
SuspectionMode* __fastcall SuspectionDeletingDestruct(SuspectionMode* mode, void*, int flags);   // 004BA6E0
AggressionMode* __fastcall AggressionClone(const AggressionMode* mode);                 // 004BA730
void __fastcall AggressionReset(AggressionMode* mode);                                  // 004BA7C0
// 004BA870 (slot 10, RET 4): the enemy is released and dropped when it is `object`.
void __fastcall AggressionObjectDeleted(AggressionMode* mode, void*, CEngineObject* object);
AggressionMode* __fastcall AggressionDeletingDestruct(AggressionMode* mode, void*, int flags);   // 004BA8A0
void __fastcall AggressionDestruct(AggressionMode* mode);                               // 004BA8C0
CorpseWatcherMode* __fastcall CorpseWatcherClone(const CorpseWatcherMode* mode);        // 004BA930
void __fastcall CorpseWatcherReset(CorpseWatcherMode* mode);                            // 004BA950
CorpseWatcherMode* __fastcall CorpseWatcherDeletingDestruct(CorpseWatcherMode* mode, void*, int flags);  // 004BA990
