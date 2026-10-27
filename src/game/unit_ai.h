#pragma once

// Unit AI: the per-unit "brain" that picks a behaviour mode every AI tick and runs it, the modes
// themselves (Guard, Patrol, Sentry, Suspection, Aggression, CorpseWatcher, Player, Fear, Tamed,
// Follow; the script's UM* commands create them) and the actions the brain issues (move, attack,
// ...). Range 005D0000-005DC000 of game.exe. Names are provisional.
// Ownership: a unit's controller (CUnit + 0x240) embeds the brain at +0x980. The controller's tick
// calls AIBrain::Think once per AI update. Modes are reference-counted objects (16-bit
// count at +4, released through 0040A3A0) kept in the brain's mode list; the brain holds a counted
// pointer to the mode in charge.

#include "ai_alarm.h"
#include "ai_map.h"
#include "core_refcount.h"
#include "engine_object.h"
#include "map_spec.h"
#include "maths.h"
#include "unit.h"

#include "lib/mfc.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class CUnit;
template <typename T>
struct AIRefTo;
struct AIBrain;
struct AIOrder;
class CUnitServer;      // combat.h (the unit controller of AI units, the brain's owner)
struct AIEventPresence;   // combat.h

struct CountedUnits : mfc::CTypedPtrList<CUnit> {
    // 005CD500 (thiscall on a counted object list, RET 4; unit_ai_memory.cpp). Appends `object`
    // holding a reference; returns the node.
    Node* AddCountedTail(CUnit* object);
    // 005D8D50 (RET 4, unit_ai.cpp; the brain's corpse list at +0x148, CorpseWatcherMode::Evaluate through
    // AIBrain::PopCorpse): RemoveHead, the head's object handed to `out` with a reference (the list's own
    // reference released). Returns `out`.
    AIRefTo<CUnit>* PopHead(AIRefTo<CUnit>* out);
};
using AIObjectList = CountedUnits;

// (The counted reference AIRefTo<T> / AIRef is in engine_object.h.)


// An order as the controller's order checks take it (0x1C bytes, built on the stack).
struct AIOrder {
    // Kind 9 (none): the object released and cleared, the flag cleared.
    void Clear();
    // Clear, then an attack (3, 6) on `object` (referenced).
    void MakeAttack(CMapObject* object);
    // Clear, then a cast (5) of `spell` at `object` (referenced).
    void MakeCast(int spell, CMapObject* object);
    // 00670760 (RET 8; player_server_handlers.cpp). Clear, then a use (6) of `object` (referenced)
    // with `what`.
    void MakeUse(CMapObject* object, int what);
    // Where the order aims: the object's AimPoint (slot 53), or `point` without
    // an object. Returns `out`.
    Vector3* AimPoint(Vector3* out) const;

    int kind;                       // +0x00 3 attack, 4/5 cast (spell or quick item), 6 use/skill, 9 none
    std::uint32_t source;           // +0x04 6 for an attack, the spell for a cast; in a UnitAction
                                    //       (combat.h) the spell slot, or quick item slot | 0x80000000;
                                    //       0x18A95 for a skill used on a target (kind 6)
    AIRefTo<CMapObject> target;     // +0x08 counted
    Vector3 point;                  // +0x0C target point; left uninitialised by the AI's orders
    std::uint8_t flag18;            // +0x18
    std::uint8_t unused_19[3];
};
static_assert(sizeof(AIOrder) == 0x1C);

// One remembered look direction of the current move (0x10 bytes).
struct AILook {
    float angle;                    // +0x00
    float time;                     // +0x04 turning time
    float wait;                     // +0x08 wait afterwards (game ticks)
    std::uint8_t stopFirst;         // +0x0C
    std::uint8_t unused_0D[3];

    // 004BA1A0 (RET 0x10; script_unit_modes.cpp; UMPatrolLook's entry): angle, wait, turning time, stop
    // first (low byte). The arguments come in a different order from the fields: the second goes to
    // +0x08, the third to +0x04. Returns this.
    AILook* ScriptAILookConstruct(float newAngle, float newWait, float newTime, int newStopFirst);
};
static_assert(sizeof(AILook) == 0x10);

// CArray<AILook> (0x14 bytes; vtable 0073D4B4, base 0073D460). SetSize 005D8EE0 (new elements
// zeroed) and Copy 005D8B60 are registered on the template (unit_ai_looks.cpp).
struct AILooks : mfc::CArray<AILook> {
    // 004CDCB0 (RET 8; script_parse_arrays.cpp; the brain's look queue and the patrol looks of
    // UMPatrol). Grown to index + 1 when needed (new looks zeroed, 004CDE10), the look copied.
    void SetAtGrow(int index, const AILook& look);
    // 005D9DE0 (RET 8; unit_ai_looks.cpp). CArray::GetAt returning by value: element `index` copied into
    // `out`, which is returned.
    AILook* CopyAt(AILook* out, int index) const;
};
using AILookArray = AILooks;
void __fastcall LooksDestroyAt(AILookArray* looks);                                  // 004CB770 (base vtable, frees the data)
// 005573F0 (unit_ai_looks.cpp). Base vtable, no data, growth step 0. Returns `looks`.
AILookArray* __fastcall LooksInit(AILookArray* looks);
// 005D9010 / 005D91B0 (fastcall, plain RET; unit_ai_save.cpp). The looks from / to a save: the
// count, then angle, time and wait of each and (files from version 0x44) the stop flag.
void __fastcall LoadLooks(AILookArray* looks, CStream* stream);
void __fastcall SaveLooks(const AILookArray* looks, CStream* stream);

// A move the modes prepare on the stack (0x20 bytes): where to go and what to look at there.
struct AIMoveOrder {
    // 00545B20 (unit_ai_looks.cpp). The point zeroed,
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

// Base of all modes (game.exe's abstract vtable 0073D15C): a reference-counted object (core_refcount.h: slots 0-3
// are CRefObject's; OnShutdown and Clone are each mode's), then 4-12 below. AIActionMode is the base of
// the modes that act.
class AIMode : public CRefObject {
public:
    // 005D8850 (unit_ai_mode_lifetime.cpp): a new mode, no references and no flags (the original's creators
    // cleared the count).
    AIMode();
    ~AIMode() override;   // 004BA330 (deleting: 004BA340; script_unit_modes.cpp)

    // 4: the priority the mode claims this tick. 005D0A80 (unit_ai_modes.cpp): always 50 (Guard, Sentry, Follow).
    virtual int Evaluate(AIBrain* brain);
    // 5: runs while the mode is in charge. 005D0A90 (unit_ai_mode_lifetime.cpp): the idle remark (1 in 1001),
    // then LookAround(0).
    virtual void Execute(AIBrain* brain);
    // 6. 005D0AF0 (unit_ai_modes.cpp; Guard, Patrol, Sentry, Follow): drop the action and target, pick a new
    // alertness decay (0.8 + 0.4 * random) and clear the controller's busy flag.
    virtual void Enter(AIBrain* brain);
    virtual void Leave(AIBrain*) {}
    virtual int GetType() = 0;                          // 8: AIModeType
    // 9: 1 for most modes; AddMode replaces a mode of another type when both answer 1. 004BA240.
    virtual bool Exclusive() { return true; }
    virtual void OnObjectDeleted(CEngineObject*) {}
    virtual void Load(CStream*) {}
    virtual void Save(CStream*) {}
};
static_assert(sizeof(AIMode) == 8);

// The abstract base of the modes that act (game.exe's vtable 0073D22C): Suspection, Aggression, CorpseWatcher,
// Fear and Player.
class AIActionMode : public AIMode {
public:
    AIActionMode();               // 005D6D10 (unit_ai_mode_lifetime.cpp)
    ~AIActionMode() override;     // 004BA6A0 (deleting: 004BA680; script_unit_modes.cpp)
    int Evaluate(AIBrain* brain) override = 0;
    void Execute(AIBrain* brain) override = 0;
    // 005D0A50 (unit_ai_modes.cpp): drop the action and target.
    void Enter(AIBrain* brain) override;
    bool Exclusive() override { return false; }
};

// Guard (game.exe's vtable 0073D128, 0x20 bytes): stay near a point. The script's modes' lifetimes and small
// slots are in script_unit_modes.cpp.
class GuardMode : public AIMode {
public:
    // A mode AIBrain::Load fills: no failures yet.
    GuardMode() { failures = 0; }
    // Set.
    GuardMode(const Vector3* point, float radius, float wander);
    ~GuardMode() override;   // 004BA320 (deleting: 004BA300)
    // No failures (EAX not set: 0).
    int OnShutdown() override;
    // A new mode with the fields copied (floats as dwords); null when the allocation fails.
    GuardMode* Clone() override;
    // Sets the point and the two radii (negative: the globals 007C540C / 007C53FC).
    void Set(const Vector3* point, float radius, float wander);
    // When idle: walk to a free place near the point (after 100 failures, near
    // the unit) and look around there.
    void Execute(AIBrain* brain) override;
    int GetType() override { return kModeGuard; }
    // 005D0EB0 / 005D0F00 (slots 11 / 12).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

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
// Add 005D93B0 and SetAtGrow 005D9F30 are registered on the template (unit_ai_looks.cpp); SetSize
// is 004C9970 (mfc::impl::AiLookRecords).
struct PatrolPoints : mfc::CArray<PatrolPoint, PatrolPointElements> {
    // 005D8CF0 / 005D95B0. The base vtable, every point's looks freed, then the buffer (pointer and
    // count kept).
    void Destruct();
    // 005D8CD0 / 005D9D00 (RET 4). Destruct, then operator delete when `flags` bit 0 is set; returns this.
    PatrolPoints* DeletingDestruct(int flags);
    // 005D9E40 / 005D9590 (RET 8; unit_ai_mode_lifetime.cpp). GetAt by value: point `index` copied into
    // `out` (a new look array); returns `out`.
    PatrolPoint* CopyAt(PatrolPoint* out, int index) const;
};
using PatrolPointArray = PatrolPoints;

// Patrol (game.exe's vtable 0073FF40, 0x20 bytes): walk a list of points.
class PatrolMode : public AIMode {
public:
    // 005D0F60 (unit_ai_mode_lifetime.cpp): no points (the array's vtables), index 0.
    PatrolMode();
    // 005D1240 (deleting: 005D1220): frees every point's looks and the points.
    ~PatrolMode() override;
    // The points freed and the mode as new (the count kept; EAX not set: 0).
    int OnShutdown() override;
    // A new mode (0x20 bytes) with copies of the points and `current`; null when the
    // allocation fails.
    PatrolMode* Clone() override;
    // 50; without points: FatalError "Unit %i has no patrol points, switching to
    // guard", a point at the unit's position, and -1000.
    int Evaluate(AIBrain* brain) override;
    // When idle: on to the next point and its looks.
    void Execute(AIBrain* brain) override;
    int GetType() override;   // 8: 005D1210, 1
    // The last point, or null.
    PatrolPoint* LastPoint();
    // 005D1930 / 005D1A40 (slots 11 / 12). The point count (the points' SetSize 004C9970), per point
    // its position, look count (the looks' SetSize 005D8EE0) and per look angle, time, wait and
    // (from version 0x44 when loading) the stop flag byte; then `current`.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // 005D12B0 (through its address, so the verify wrapper sees Evaluate's calls).
    void AddPoint(const Vector3* position);
    // 005D12B0 (unit_ai_looks.cpp). Appends a point with no looks at ((n + 0.5) / 2, z 0) for
    // n = 2x rounded to nearest (so 0..0.5 above `position`, not the middle of its half-cell).
    void AppendPoint(const Vector3* position);

    PatrolPointArray points;        // +0x08 (vtables 0073FF74 / 0073FF80)
    int current;                    // +0x1C
};
static_assert(sizeof(PatrolMode) == 0x20);

// Sentry (game.exe's vtable 0073D190, 0x14 bytes): stand at a point.
class SentryMode : public AIMode {
public:
    SentryMode() {}   // a mode AIBrain::Load fills
    // Set.
    explicit SentryMode(const Vector3* point);
    ~SentryMode() override;   // 004BA440 (deleting: 004BA420)
    int OnShutdown() override;        // 2: 004BA3F0, nothing (EAX not set: 0)
    SentryMode* Clone() override;     // 3: 004BA3B0, as GuardMode's
    // Sets the point.
    void Set(const Vector3* point);
    // When idle: back to the point and look around.
    void Execute(AIBrain* brain) override;
    int GetType() override;   // 8: 004BA410, 2
    // 005D1DA0 / 005D1DC0 (slots 11 / 12).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 point;                  // +0x08
};
static_assert(sizeof(SentryMode) == 0x14);

// Follow (game.exe's vtable 0073D1C4, 0x0C bytes): stay with a leader.
class FollowMode : public AIMode {
public:
    // The leader referenced (none: a mode AIBrain::Load fills).
    explicit FollowMode(CMapObject* leader = nullptr);
    ~FollowMode() override;   // 004BA5B0 (deleting: 004BA590): the leader released and dropped
    int OnShutdown() override;        // 2: 004BA510, the leader released and dropped (EAX not set: 0)
    FollowMode* Clone() override;     // 3: 004BA4A0, the leader referenced
    // When idle: action 6 (follow) with the leader as target.
    void Execute(AIBrain* brain) override;
    int GetType() override;   // 8: 004BA580, 10
    // Drops the leader when it is the deleted object.
    void OnObjectDeleted(CEngineObject* object) override;
    // 005D1E60 / 005D1E80 (slots 11 / 12). Loads / saves the leader's reference.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> leader;     // +0x08
};
static_assert(sizeof(FollowMode) == 0x0C);

// Suspection (game.exe's vtable 0073D1F8, 0x18 bytes): go and look where something was noticed.
class SuspectionMode : public AIActionMode {
public:
    // The original's constructor stores the vtable only; ours zeroes the point and level too
    // (ClearFields).
    SuspectionMode() { ClearFields(); }
    // 004BA610 (registered: the harness redirects game.exe's calls of it). The constructor in place.
    SuspectionMode* Construct();
    ~SuspectionMode() override;   // 004BA700 (deleting: 004BA6E0)
    int OnShutdown() override;            // 2: 004BA6B0, nothing (EAX not set: 0)
    SuspectionMode* Clone() override;     // 3: 004BA630, as GuardMode's
    // Remembers a non-allied seen object, then takes over the brain's noticed
    // point and level (the level only grows down to the noticed one while the point is the same).
    int Evaluate(AIBrain* brain) override;
    // Walk to the noticed point; near it the interest fades.
    void Execute(AIBrain* brain) override;
    // Busy; walk to the point; remark 5.
    void Enter(AIBrain* brain) override;
    // Clears busy; remark 3.
    void Leave(AIBrain* brain) override;
    int GetType() override;   // 8: 004BA6D0, 3
    // 005D2330 / 005D2360 (slots 11 / 12).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // Point and level zeroed. The original's constructor leaves them, but Evaluate keeps a point that compares
    // the same (a NaN always does) and the lower level, so every new mode starts from them zeroed: a
    // determinism fix, SV7d4; the harness zeroes the original's alike (tests/verify/harness_leftovers.h).
    void ClearFields() {
        point = {};
        level = 0;
    }

    Vector3 point;                  // +0x08
    int level;                      // +0x14
};
static_assert(sizeof(SuspectionMode) == 0x18);

// A target the attention chose or the brain is attacking: the part (-1 the whole unit with a weapon,
// else the spell's part) and the object.
struct AITargetChoice {
    int part;                       // +0x00
    CUnit* object;                  // +0x04
};
static_assert(sizeof(AITargetChoice) == 8 && offsetof(AITargetChoice, object) == 4);

// Aggression (game.exe's vtable 0073D260, 0x18 bytes): fight an enemy.
class AggressionMode : public AIActionMode {
public:
    // No enemy, the whole unit as the part to hit, enemy not fixed. 004BA830 (RET 4; UMRevenge): the
    // same with `fixed` given.
    AggressionMode();
    explicit AggressionMode(std::uint8_t fixed);
    ~AggressionMode() override;   // 004BA8C0 (deleting: 004BA8A0): the enemy released
    // The enemy released, then the mode as new (EAX not set: 0).
    int OnShutdown() override;
    // References the enemy and copies `chosen` without a reference.
    AggressionMode* Clone() override;
    // 10000 while there is an enemy to fight (the script's, or the best one
    // in sight), else -1000.
    int Evaluate(AIBrain* brain) override;
    // Attack the chosen enemy (or cast spell `chosen.part` at it).
    void Execute(AIBrain* brain) override;
    // Busy; drops action and target; remark 6.
    void Enter(AIBrain* brain) override;
    // Forgets the noticed level when the enemy is gone or dead, drops the enemy,
    // clears busy; remark 4.
    void Leave(AIBrain* brain) override;
    int GetType() override;   // 8: 004BA860, 4
    // The enemy released and dropped when it is `object`.
    void OnObjectDeleted(CEngineObject* object) override;
    // 005D2770 / 005D27A0 (slots 11 / 12). The enemy's reference and `fixed`.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> enemy;      // +0x08
    std::uint8_t fixed;             // +0x0C enemy set by the script (no searching)
    std::uint8_t unused_0D[3];
    AITargetChoice chosen;          // +0x10 the enemy chosen this tick; part -1: the whole unit
};
static_assert(sizeof(AggressionMode) == 0x18);

// CorpseWatcher (game.exe's vtable 0073D294, 8 bytes): reports corpses the brain has seen (slots 11 / 12 the
// base's empty ones).
class CorpseWatcherMode : public AIActionMode {
public:
    CorpseWatcherMode();
    ~CorpseWatcherMode() override;       // 004BA9B0 (deleting: 004BA990)
    int OnShutdown() override;           // 2: 004BA950, nothing (EAX not set: 0)
    CorpseWatcherMode* Clone() override;
    // Takes one seen corpse off the brain's list and notes it as noticed
    // (level 800, kind -3). Never takes charge (-1000).
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain*) override {}
    int GetType() override;              // 8: 004BA980, 5
};

// Fear (game.exe's vtable 0073FF8C, 0x2C bytes): run from an enemy.
class FearMode : public AIActionMode {
public:
    // 005D2850 (unit_ai_brain.cpp): distance from 0073FF00, 3, an unset point (x = -1313), +0x24 = 0 and
    // `flag`. The original leaves shelter y / z and fleePoint x / y, which the saves write: ours zeroes every
    // field first (ClearFields; a determinism fix, SV7b2; the harness zeroes the original's alike).
    explicit FearMode(std::uint8_t flag);
    // 005D2850 (registered: the harness redirects game.exe's calls of it). The constructor in place.
    FearMode* Construct(std::uint8_t flag);
    void ClearFields() { std::memset(&distance, 0, sizeof(FearMode) - offsetof(FearMode, distance)); }
    ~FearMode() override;   // 005D2950 (deleting: 005D2930; unit_ai_mode_lifetime.cpp)
    // 005D28F0 (slot 2; unit_ai_mode_lifetime.cpp). The constructor's stores without the flag (the count and
    // +0x14 .. +0x20 stay). EAX is 0 on return.
    int OnShutdown() override;
    // 005D2880 (slot 3; unit_ai_mode_lifetime.cpp). A new mode with this one's fields; null when the
    // allocation fails.
    FearMode* Clone() override;
    int GetType() override;   // 8: 005D2920, 7
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
    int Evaluate(AIBrain* brain) override;
    // 005D30A0 (slot 5; unit_ai_fear.cpp). Runs from the nearest enemy (calling allies to it), else
    // stays in shelter, else from the noticed point (forgotten unless `flag` and above 500): within
    // `distance`, and not walking on command, to `fleePoint` when one is pending, else 5 away plus
    // a random offset of at most `spread` (which first drops to 2.5 x `distance`), when a path is
    // found (action 1); `spread` doubles. Without an action: attack the best target in sight
    // (allies only, 005D0030), else stand and look away from the point.
    void Execute(AIBrain* brain) override;
    // Busy; drops action and target.
    void Enter(AIBrain* brain) override;
    // Clears busy.
    void Leave(AIBrain* brain) override;
    // 005D3690 / 005D36F0 (slots 11 / 12).
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    float distance;                 // +0x08 how near danger counts (10, FearDefaultDistance)
    float spread;                   // +0x0C random spread of a flight point (3)
    Vector3 shelter;                // +0x10 the last place with a kind 3 occluder (x = -1313: unset)
    Vector3 fleePoint;              // +0x1C a checked flight point (z 1.0 while pending, else 0)
    std::uint8_t flag;              // +0x28
    std::uint8_t unused_29[3];

private:
    // The constructor's stores (and OnShutdown's).
    void Init(std::uint8_t flag);
};
static_assert(sizeof(FearMode) == 0x2C);
static_assert(offsetof(FearMode, flag) == 0x28);
static_assert(offsetof(FearMode, fleePoint) == 0x1C);
// A new Fear mode's distance, which `distance` returns to.
inline float FearDefaultDistance() { return 10.0f /*0073FF00*/; }

// Player (game.exe's vtable 0073F0F8, 0x24 bytes): the party's units. Its lifetime and small slots are in
// unit_ai_player_mode_slots.cpp (A-U6).
class PlayerMode : public AIActionMode {
public:
    enum { kStateFollow = 0, kStateCommand = 1, kStateMoveTo = 2 };

    // 005D3760 (unit_ai_mode_lifetime.cpp). No target or leader, point 0, state 0, not running.
    PlayerMode();
    // 005520D0 (deleting: 005520B0): releases the leader, then the target.
    ~PlayerMode() override;
    // The references released, then the mode as new; EAX the mode.
    int OnShutdown() override;
    // A new mode holding the same target and leader (one more reference each) with the
    // same point, state and run; null when the allocation fails.
    PlayerMode* Clone() override;
    int Evaluate(AIBrain* brain) override;   // 4: 00552090, 10
    int GetType() override;                  // 8: 005520A0, 6

    // State 0: follow the leader (unless the leader is moving itself) and fight
    // state 1: wait for the commanded action to end; state 2: walk to `point` (staying
    // with the party unless the player controls the unit directly) and fight on the way.
    void Execute(AIBrain* brain) override;
    // Drops either reference when it is the deleted object.
    void OnObjectDeleted(CEngineObject* object) override;
    // 005D4CD0 (slot 11 of both vtables; unit_ai_modes.cpp, A-W14). The target's and the leader's
    // references, the point and the state. Saves of versions 0x46 .. 0x5D hold a group of objects
    // after them (ScriptGroup::Load), which is read and dropped.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // The player's commands (state 1 or 2: the unit does what it was told).
    // Walk to `to` + `offset` (just `to` when the way there is blocked); forgets the leader
    // unless the player controls the unit directly.
    void CommandWalk(AIBrain* brain, const Vector3* to, int unknown, int run, const Vector3* offset);
    // The same in state 2 (keep fighting on the way); when the unit would leave the party
    // it only turns towards the point.
    void CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int run, const Vector3* offset);
    // Attack `object`.
    void CommandAttack(AIBrain* brain, CMapObject* object, int how, int unknown);
    // Use something on `object` (AIBrain::UseOn).
    void CommandUse(AIBrain* brain, CMapObject* object, int kind, int a, int b);
    // Cast spell `spell` at `object`.
    void CommandCast(AIBrain* brain, int spell, CMapObject* object, int unused);
    // Cast spell `spell` at `point`.
    void CommandCastAt(AIBrain* brain, int spell, const Vector3* point, int unused);

    // Follow `leader` (counted): none when it is the unit itself. When the chain
    // of leaders from `leader` (each unit's first Player mode) comes back to this unit, this unit's
    // own Player mode stops following first. A walk in progress (action 1) is dropped (state 0).
    void SetLeader(AIBrain* brain, CUnit* leader);

    // unit_ai_player.cpp.
    // Only for units whose +0x438 is 1: the mode's target when it can be reached
    // from here (attacked at once); else the best target (005D0030, no allies) among the enemies
    // near `point` (within [Logic] SwarmRadius in state 2, anywhere otherwise, the point then
    // zeroed) that are no farther than 1.1 x their own sight or hearing, plus what the party is
    // fighting. When there is one, the party's other followers that are not attacking or casting
    // stop (unless the player controls the unit directly), the unit attacks it unless it already
    // does, and the mode's target is dropped.
    void UpdateCombat(AIBrain* brain);
    // Adds to `targets` (once each) the units that party members within [Logic]
    // PlayerCallForHelp are attacking (action 3), unless they belong to the same player.
    void AddPartyTargets(AIBrain* brain, AIObjectList* targets);
    // `party` emptied, then the unit, the chain of leaders above it (Player
    // mode's leader, or the followed unit), and repeatedly every unit of its player whose leader is
    // listed (the player's units in hash order, up to the first null one).
    void CollectParty(AIBrain* brain, AIObjectList* party);
    // 005D4860 (RET 8; only AL). False when a unit of the player whose (first) Player mode follows
    // this unit is farther than [Logic] FollowMaxDist x (2 while `moving`, else 1.4) + 1 (3D).
    bool StaysWithParty(AIBrain* brain, bool moving);
    // 005D37E0 (unit_ai_moves.cpp). Can the unit get from `from` to `to`: false when they are within
    // 0.1 (2D), else a path search with the unit moved to `from` for it (range twice the distance).
    bool PathClear(AIBrain* brain, const Vector3* from, const Vector3* to);

    AIRefTo<CMapObject> target;     // +0x08
    AIRefTo<CUnit> leader;          // +0x0C
    Vector3 point;                  // +0x10 (z: 1.0 while walking there in state 2)
    int state;                      // +0x1C
    std::uint8_t run;               // +0x20
    std::uint8_t unused_21[3];

protected:
    // Both OnShutdowns: the references released, then the constructor's stores.
    void Reset();
};
static_assert(sizeof(PlayerMode) == 0x24);

// Tamed (game.exe's vtable 0073F0C4, type 8): the Player mode a taming installs (combat_taming.cpp), with its
// own slots 0, 2, 3, 4 and 8 (unit_ai_player_mode_slots.cpp).
class TamedMode : public PlayerMode {
public:
    TamedMode() {}
    ~TamedMode() override;   // 00552180 (deleting: 00552160): the player's body
    int OnShutdown() override;               // 2: 00551EC0, as the player's
    TamedMode* Clone() override;             // 3: 00551E10, as the player's
    int Evaluate(AIBrain* brain) override;   // 4: 00552140, 100000000
    int GetType() override;                  // 8: 00552150, 8
};


// 0052AF20 (counted_ref.cpp; fastcall, RET 4): a count read from `stream`, then that many counted
// references appended to `list`, each loaded in `context` (the trap's units, the brain's lists).
void __fastcall CountedListLoad(mfc::CTypedPtrList<CUnit>* list, CStream* stream, CWorld* context);

class CSpell;
namespace console {
class GameOStream;
}
// The stream the AI's perception messages go to (the AI stream; unit_ai.cpp).
console::GameOStream* AiLog();

// 005CA050 (ECX = out, EDX = point; unit_ai_brain.cpp). Copies x and y of a point (the AI events'
// and the heard calls' noticed points); returns `out`.
float* __fastcall CopyPoint2(float* out, const float* point);

// A call the perception memory heard (0x18 bytes; CallAllies sends kind 0x1A).
struct AICallRecord {
    AICallRecord* DeleteScalar(unsigned flags);
    Vector3 point;                  // +0x00
    int kind;                       // +0x0C
    int hostileMask;                    // +0x10 sides the hearer turns hostile to (0 for a plain call)
    mfc::CString text;              // +0x14

    // 00558D00 (unit_ai_brain.cpp). The element destructor: frees the text (CString::~CString).
    void Destruct();
    // The element assignment of the array's Add (unit_ai_brain.cpp): the point, kind and +0x10,
    // then the text (CString::operator=).
    AICallRecord& operator=(const AICallRecord& other);
};
static_assert(sizeof(AICallRecord) == 0x18 && offsetof(AICallRecord, text) == 0x14);

// The calls' ConstructElements (00558D60: zeroed, each text the empty CString) and
// DestructElements (00558D00 each); unit_ai_brain.cpp.
struct AICallElements {
    static void Construct(AICallRecord* calls, int count);
    static void Destruct(AICallRecord* calls, int count);
};

struct AIMemory;

// CArray<AICallRecord> (the perception memory's +0xA8, brain + 0x178; thiscall on the array).
// SetSize 005CDD60 and Add 005CD880 are registered on the template (unit_ai_brain.cpp).
struct AIHeardCalls : mfc::CArray<AICallRecord, AICallElements> {
    // 005CC9D0 (RET 8; unit_ai_brain.cpp; each tick, from AIMemory::Update). Counts every call
    // down; one that runs out is dropped after it is heard: with a hostility mask (+0x10) the
    // unit's controller takes the mask (logged "hear delayed sound"), else, when the memory's
    // noticed level is below 900, the call's point becomes the noticed point (900, kind -3; logged
    // when the level rose by more than 100 or the point lies more than sqrt(10) away).
    void Update(CUnit* unit, AIMemory* memory);
    // 005CCD40 (RET 0xC; unit_ai_hearing.cpp; the memory's HearCall 005CBBA0 and AIViewer::Noise
    // inline it). The calls without a mask whose kind is `kind` or above are dropped, then {point,
    // kind, 0, text} is appended.
    void Hear(const Vector3* point, int kind, const char* text);
    // 005CCEA0 (RET 0xC; the memory's 005CBBC0). Appends {point not set, kind, mask, text}: when it
    // runs out (Update) the unit's controller takes the hostility mask.
    void AddClaim(std::uint32_t mask, int kind, const char* text);
    // 005CCBF0 (plain RET). Drops the calls that carry a mask.
    void RemoveClaims();
};

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
// An object the memory reported (8 bytes; the object is a counted reference).
struct AIReport {
    AIRefTo<CMapObject> object;
    int what;
};
static_assert(sizeof(AIReport) == 8);

// The reports' ConstructElements (00558D10: zeroed, each object null) and DestructElements
// (00558CE0 each: the object released, null); unit_ai_brain.cpp.
struct AIReportElements {
    static void __stdcall Construct(AIReport* reports, int count);
    static void Destruct(AIReport* reports, int count);
};

// CArray<AIReport> (the perception memory's +0x94, brain + 0x164; thiscall on the array,
// unit_ai_brain.cpp). SetSize 005CD670 and RemoveAt 005CD800 are registered on the template (A-W12).
struct AIReports : mfc::CArray<AIReport, AIReportElements> {
    // Keeps the larger `what` of a listed object, else appends it (with a reference).
    void Report(CMapObject* object, int what);
    // Whether `object` is listed (only AL is set).
    bool Contains(CEngineObject* object) const;
};

// One way the unit can attack or help (0x28 bytes): a weapon (part -1) or a spell.
struct AIAttackOption {
    // 005CE9B0 (ST0; unit_ai_options.cpp). How much the option counts: kind 0 its largest damage,
    // 1..3 that * 1.3, 4..6 1, 7 10, other kinds 0.
    double Weight() const;
    // 005CE860 / 005CE920 (RET 4; unit_ai_options.cpp). The option from / to a save. Before
    // version 0x5A only the part, a dropped dword and the damage were saved: the kind becomes
    // part >= 0, the range 1, the rest 0.
    void Load(CStream* stream);
    void Save(CStream* stream) const;

    int kind;                       // +0x00 0..3 attacks (2, 3 hit an area), 4..6 helping spells, 7 healing
    int part;                       // +0x04 -1 a weapon, else the spell
    float range;                    // +0x08
    int maxCount;                   // +0x0C cap on the group-size bonus
    float radius;                   // +0x10 area of kinds 2 and 3
    int effect;                     // +0x14 effect type (kinds 4..6), strength (kind 3)
    std::uint16_t damage[8];        // +0x18 measured against the target's protection
};
static_assert(sizeof(AIAttackOption) == 0x28);

// CArray<AIAttackOption> (0x14 bytes; zero-filled elements). Its SetSize 005D9250 is registered on
// the template (unit_ai_options.cpp).
using AIAttackOptions = mfc::CArray<AIAttackOption>;
static_assert(sizeof(AIAttackOptions) == 0x14);
// 005D9B80 (unit_ai_options.cpp; fastcall, plain RET): the count, the array sized to it, then each option.
void __fastcall LoadAttackOptions(AIAttackOptions* options, CStream* stream);


// The brain's attention (+0xA4, 0x2C bytes): the unit's attack options and choosing a target
// (unit_ai_targets.cpp; the methods are __thiscall on the attention, the brain passed).
struct AIAttention {
    // The best {part, object} to attack or help into `choice`: the
    // current attack scored first, then (without `alliesOnly`) every weapon against every
    // candidate and every spell against the unit and its allies in sight; the best that scores
    // above the current one and passes IsUsable, the order check (with `alliesOnly`, for targets
    // not near the path) and CanReach wins; a rejected one seen once is reported (20).
    void Choose(AIBrain* brain, AITargetChoice* choice, AIObjectList* candidates, const Vector3* from, bool alliesOnly,
                int reachArg);
    // False when the part has no option or an area option (kinds 2, 3) would
    // also hit an ally near the target or the unit itself; true without an object.
    bool IsUsable(AIBrain* brain, const AITargetChoice* choice);
    // 005CF9A0 (RET 0x1C; `this` unused). How good `option` is against `candidate` (`group` the
    // others it would hit or help): by kind, minus 25 per unit of distance beyond the range (x10
    // outside kinds 0..4) when it reached `threshold`, minus twice the hits needed for attacks.
    // -1e30 when it cannot apply. Returned unrounded (ST0).
    double Score(const Vector3* from, CUnit* self, AIObjectList* group, CEngineObject* current, CUnit* candidate,
                 float threshold, const AIAttackOption* option);
    // 005CF3C0 (RET 0xC; only AL). Whether the unit may attack / cast at the target: false for a
    // spell aimed at no object against a large unit of size class 0; else the order is checked
    // from the unit's place when `fromHere` (low byte) is set or the target is the
    // unit itself, otherwise from the end of a path search to the target (range ScaledValue of the
    // distance, the target lifted off the sector map meanwhile), or the unit's place without one.
    bool CanReach(AIBrain* brain, const AITargetChoice* choice, int fromHere);

    // 005CFE40 (RET 8; unit_ai_attention.cpp). Chooses the best target in sight (Choose from the
    // unit's position) and, when it differs from the current attack, attacks it (action 3; the
    // player's order check may lift the flags to -1) or casts spell `part` at it (action 4 when the
    // spell cannot be cast now).
    void LookAround(AIBrain* brain, int unused);
    // 005CF220 (RET 8; unit_ai_attention.cpp). The brain's current attack as {part, object}: action 3
    // {-1, target}, action 4 {spell, target}, the object only when it is a unit; otherwise only the
    // object is cleared.
    void CurrentAttack(AIBrain* brain, AITargetChoice* choice);
    // 005CF170 (RET 8; unit_ai_fear.cpp). Takes up {part, object} unless there is no object or it is
    // the current attack: part -1 AttackTarget, else cast it (action 4 when the spell cannot be cast
    // now; target and actionTime = the spell).
    void ApplyAttack(AIBrain* brain, const AITargetChoice* choice);

    // The option of `part`: the weapons first, then the spells; null when there is none.
    const AIAttackOption* FindOption(int part) const;

    // unit_ai_options.cpp:
    // Lists the unit's options: its weapon (unless its natural weapon is 0x10,
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
// updated's position, eye and facing (unit_ai_sight.cpp; __thiscall on the viewer, the brain's
// perception memory passed).
struct AIViewer {
    // 005CAA00 (RET 8; AIMemory::Update each brain tick). The viewer moves to the owner's unit, the
    // nearby units are listed again when due (Refresh), units in the visible list it no longer
    // sees are dropped ("lost from sight": an enemy notices 1000 / -2; a corpse is listed as one
    // first, an allied one noticing 800 / -3), then new ones are looked for (See).
    void Update(CUnitServer* owner, AIMemory* memory);
    // Every 8..11 updates (random): the units of the sector cells within the
    // larger of the unit's two sight ranges become the nearby list (+0xF4, counted).
    void Refresh(CUnit* self, AIMemory* memory);
    // Nearby units not yet visible (corpses not yet listed) that the unit sees:
    // within sight range, inside the view cone and the line of sight; or with peripheral vision;
    // or sensed alive. Players' units see everything; others only enemies.
    void See(CUnit* self, AIMemory* memory);
    // Raises the eye by the AI map's ground level of its cell and the unit's
    // height parts and sets the facing from the unit's orientation (once per update).
    void Prepare(CUnit* self);
    // 005C98C0 (RET 0x10; unit_ai_hearing.cpp; attacks, skill uses, explosions). A noise of
    // `loudness` made by `source`'s unit at `at` (null: the unit's position): every unit within 50
    // of the point (the sector map) that is hostile to the source's unit, whose noticed level is
    // below 900 and whose hearing (moveFactors[1] x valuesB0[3]) times the loudness reaches past
    // the distance hears a call of `kind` at the source unit's position (AIHeardCalls::Hear; the text
    // "Unit %i heard unit %i from %fm distance" only with console filter bit 0).
    void Noise(CUnitServer* source, float loudness, int kind, const Vector3* at);
    // 005C9770 (RET 8; unit_ai_hearing.cpp, A-W14; the controller's update every few frames). The
    // noise of a moving unit: Noise of kind 0x1A at the unit, as loud as moveFactors[0] x
    // valuesE0[3] x the noise of the ground at `position` (the AI map's terrain type). Returns 0
    // (no presence record; the controller keeps the result at +0x918).
    AIEventPresence* StepNoise(const Vector3* position, CUnitServer* source);

    CAIMap* map;                    // +0x00
    int tick;                       // +0x04 the world's event table: its tick (AIEventTable::Advance)
    Vector3 eye;                    // +0x08
    Vector3 facing;                 // +0x14
    int cellX;                      // +0x20 AI map cell of the eye
    int cellY;                      // +0x24
    std::uint8_t ready;             // +0x28 Prepare has run
    std::uint8_t unused_29[3];
};
static_assert(offsetof(AIViewer, facing) == 0x14);

// 005CD210 (fastcall, RET 4; unit_ai_sight.cpp; the show-visibility effect's tick 0067C840). The
// cells `seer` sees as `observer` would be seen, into `grid` (cleared to one cell first): within
// sight (the two units' sight values, at least 0) inside the cone, up to the observer's mode
// height; within hearing all around; within the peripheral range in the front half.
void __fastcall MarkSightCells(CUnit* seer, CUnit* observer, FogGrid* grid);
static_assert(offsetof(AIViewer, ready) == 0x28);

// An AI event (0x30 bytes): filed in its map cell's list for its side and in the time wheel's slot
// of the tick it expires. Records come from the pool CList at 007C43A8 (AIEventPool) and go back to
// the free list at 007C53F0 (chained through `next`).
struct AIEvent {
    float x, y;                     // +0x00 where; its cell is (x / 8, y / 8) rounded
    std::uint32_t origin[2];            // +0x08 x and y again (005C9630 sets them; saved)
    int expiry;                     // +0x10 the tick it goes; wheel slot (expiry - 1) & 0x3FF
    std::uint32_t payload[2];           // +0x14 saved and loaded; left to the creator's caller
    int sourceId;                   // +0x1C the unit that made it
    int side;                       // +0x20 its low 4 bits: the side list it is filed in
    AIEvent* next;                  // +0x24 the cell's list for the side; the free list's link
    AIEvent* prev;                  // +0x28 null for the cell's first
    AIEvent* nextInSlot;            // +0x2C the wheel slot's list
};
static_assert(sizeof(AIEvent) == 0x30);
// 005CDBA0 (ai_events.cpp, S3c; only the exit function 005C8FB0 calls it, on the pool 007C43A8):
// CList<AIEvent>'s destructor: its vtable back, RemoveAll.
void __fastcall AIEventPoolDestruct(mfc::CList<AIEvent>* pool);

// A map cell of the event grid: the newest event of each side (lists through AIEvent::next).
struct AIEventCell {
    AIEvent* bySide[16];
};

class CStream;       // core_streams.h

// The world's AI events (CWorldServer + 0x2838; its viewer part makes the noises): a time wheel of
// 1024 ticks x 16 sides and a grid of 8 x 8 cells (rows of width cells). Its tick is the viewer's
// `tick` (Advance sets it).
// The free event records, chained through AIEvent::next (ai_grids.cpp; the pool is ai_events.cpp's).
extern AIEvent* g_freeEvents;

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
    // Tick -1, the wheel emptied (its events are not freed), the grid back to 1 x 1
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

// 005CFE20 (__fastcall, unit_ai_targets.cpp). The squared 2D distance, unrounded (ST0).
double __fastcall AIDistance2(const Vector3* a, const Vector3* b);

// The brain's perception memory (brain + 0xD0, 0xC0 bytes): what the unit sees, has seen and heard
// (unit_ai_memory.cpp, unit_ai_brain.cpp, unit_ai_hearing.cpp, unit_ai_save.cpp; the original's
// functions 005CB0F0..005CC350 are __thiscall on it).
struct AIMemory {
    // 005CB0F0 (unit_ai_brain.cpp). Empties the four object lists, the noticed point, the report
    // and call arrays.
    void Reset();
    // 00545CB0 (combat_lifetime.cpp; 00546230 calls it). Destroyed: calls, reports, then the four
    // object lists (references released, blocks freed).
    void Destruct();
    // 005CB700 (RET 4; unit_ai_brain.cpp). Ages the reports (one tick off each; an entry below 0 is
    // dropped), updates the heard calls and the AI events' view (005CAA00 on 007B2798),
    // then adds the noticed kind to the noticed level. Both callees may set the noticed point, level
    // and kind inline first (900 / -3, 800 / -3, 1000 / -2).
    void Update(CUnitServer* owner);
    // 005CB7F0 (RET 8; unit_ai_brain.cpp). 1 when `other` has the unit's side or player or is in
    // the visible list, else 0.
    int IsEnemy(CUnit* self, CEngineObject* other);
    // 005CBBE0 (RET 0xC; unit_ai_brain.cpp). Takes over a noticed point of a higher `level`: 1 when
    // the level rose by more than 100 or the point lies more than sqrt(10) away (with the point's
    // own z, not a difference), else 0; 0 when the level is not higher.
    int Notice(const Vector3* point, int level, int kind);
    // 005CC020 (RET 4; unit_ai_brain.cpp). Drops `object` from the four lists and the reports
    // (holding a reference meanwhile).
    void Forget(CEngineObject* object);
    // 005CBBA0 / 005CBBC0 (RET 0xC; unit_ai_hearing.cpp): the calls hear / add a claim.
    void HearCall(const Vector3* point, int kind, const char* text);
    void AddClaim(std::uint32_t mask, int kind, const char* text);
    // 005CB260 (RET 8; unit_ai_memory.cpp). `out` emptied, then the visible units hostile to the
    // unit (its IsHostileTo), alive and not reported, in visible-list order (no references taken).
    void FindEnemies(CUnit* self, AIObjectList* out);
    // The same with the visible unit's own IsHostileTo(self); one at or beyond
    // `radius` of `point` (3D; all of them for a radius <= 0) only when it has an aggression mode
    // (not a fixed one without an enemy) whose enemy is none, not a unit, or a unit of a side the
    // unit's side is allied with, and its brain sees an enemy.
    void FindEnemiesNear(CUnit* self, AIObjectList* out, const Vector3* point, float radius);
    // The nearest (3D) visible unit hostile to the unit and alive, or null.
    CUnit* NearestEnemy(CUnit* self);
    // Forces `object` into sight when it is not visible and attacks the unit or
    // stands at or beyond one of the two sight ranges: a corpse joins the seen corpses (and the
    // corpses when allied), a living unit the visible list's head (both counted); each logged.
    void ForceSee(CUnit* self, CUnit* object);
    // 005CBE90 (RET 8; the server's OnDamaged). Logs and adds `corpse` to the seen corpses (and to
    // the corpses when its side is allied with the unit's), both counted.
    void SeeCorpse(CUnit* self, CUnit* corpse);
    // 005CB850 (RET 8; each server tick). The unit's exposure to the living hostile units nearby
    // into `out` (5 floats, CUnit + 0x4C8): per kind the smallest distance / range ratio, at most 3
    // (a zero range counts 3). [0] sight (scaled by the line of sight between the eyes, only within
    // range), [4], [1], [2] three other ranges; [3] stays 3.
    void MeasureExposure(CUnitServer* self, float* out);
    // 005CC1E0 / 005CC350 (RET 4; unit_ai_save.cpp). From / to a save: the four object lists
    // (appended to on load), the noticed point, level and countdown, the reports, the heard calls
    // (point, kind, text, +0x10) and the noticed kind (-1 until read).
    void Load(CStream* stream);
    void Save(CStream* stream);

    SpiralCache spareCells;         // +0x00 set empty by the brain's constructor; not read
    SpiralCache sightCells;         // +0x08 the cells around the unit it looks for units in
    float noticedX;                 // +0x10
    float noticedY;                 // +0x14
    int noticedLevel;               // +0x18
    int noticedKind;                // +0x1C
    int nearbyCountdown;            // +0x20 updates until the nearby units are listed again
    AIObjectList nearby;            // +0x24 the units in the sight cells around (counted references)
    AIObjectList visible;           // +0x40 units in sight
    AIObjectList seenCorpses;       // +0x5C corpses seen (counted references)
    AIObjectList corpses;           // +0x78 allied corpses seen (counted references)
    AIReports reports;          // +0x94 objects reported to the alarm code
    AIHeardCalls calls;              // +0xA8 calls heard
    std::uint8_t seesEnemy;         // +0xBC
    std::uint8_t unused_BD[3];
};
static_assert(offsetof(AIMemory, noticedX) == 0x10);
static_assert(offsetof(AIMemory, nearby) == 0x24);
static_assert(offsetof(AIMemory, reports) == 0x94);
static_assert(offsetof(AIMemory, calls) == 0xA8);
static_assert(offsetof(AIMemory, seesEnemy) == 0xBC);
static_assert(sizeof(AIMemory) == 0xC0);

// The brain (controller + 0x980, 0x190 bytes; the controller's voice object follows at +0xB10).
struct AIBrain {
    // Actions (`action`).
    enum {
        kActionNone = 0,
        kActionWalk = 1,
        kActionMove = 2,        // move to `move.point`, then the queued looks
        kActionAttack = 3,
        kActionCast = 4,
        kActionUse = 5,
        kActionFollow = 6,
    };
    static constexpr int kNoTime = -10000;          // actionTime: no look in progress
    static constexpr int kLookStarted = -9999;      // actionTime: the first queued look is turning

    // One AI tick: alarm countdown, decay of alertness, perception update, then every
    // mode's Evaluate; the highest (the current mode gets +100) takes over (Leave / Enter, with a
    // "new motivation" console message), its Execute runs, and the brain carries out its action.
    void Think();

    // The mode of type `type` in the mode list, or null.
    AIMode* FindMode(int type);
    // 005D5E30 / 005D5E60. The Patrol / Player mode, or null.
    PatrolMode* FindPatrolMode();
    PlayerMode* FindPlayerMode();   // (the Tamed mode is a Player mode too)
    // The Player mode's target (+0x0C of the mode), or null.
    CUnit* PlayerModeLeader();
    // The same, or the action target while following (action 6).
    CMapObject* LeaderOrFollowed();
    // The Aggression mode's enemy while that mode is in charge, else null.
    CMapObject* AggressionEnemy();
    // Is `point` within `radius` of the segment from the previous to the current position
    // (both 2D)? The original sets only AL in one path; callers test AL.
    bool IsNearPath(const Vector3* point, float radius);
    // Sets the owner; on a change both remembered positions become "unset" (-1313, 0, 0).
    // Returns what the original leaves in EAX.
    std::uint32_t SetOwner(CUnitServer* newOwner);
    // An object is being deleted: tell every mode, drop it as target or seen object and
    // from the perception memory.
    void OnObjectDeleted(CEngineObject* object);

    // Mode list and state (unit_ai_brain.cpp).
    // Adds `mode` (a counted reference, RefPtr::Share) at the end of the list after
    // removing the first mode of the same type, or the first exclusive one when `mode` is exclusive
    // (the mode in charge loses its reference when it is the one removed).
    void AddMode(AIMode* mode);
    // Removes every mode of `type` (the mode in charge is left alone).
    void RemoveModesOfType(int type);
    // Empties the mode list, drops the mode in charge, resets the near point (+0x44 /
    // +0x48) and the decay.
    void ClearModes();
    // ClearModes, then no action, memory reset, action point and looks cleared, target
    // and seen object released (the controller's death).
    void Reset();
    // The state of a new controller's brain: Reset's work plus owner, alert and alarm
    // state, alarm records, positions.
    void Init();
    // combat_lifetime.cpp (A-U7). 00545990. The constructor (CUnitServer's, on +0x980): empty
    // containers with their vtables, the move order cleared, both sight caches
    // {0, -1}, then Init. Returns this.
    AIBrain* Construct();
    // 005457E0 (reached only from the unwind funclets of the controller's constructor and
    // destructor). Init, the memory destroyed (as 00545CB0), then the attention arrays, alarm
    // records, the mode in charge, the mode list, target / seen and the looks.
    void Destruct();
    // 005D8310 / 005D8860 (RET 4; unit_ai_brain.cpp). The brain from / to a save: the action, its
    // point, looks and time, target and seen object, the scalars, the modes (count, the index of
    // the one in charge, then each mode's type and its own data), the perception memory, the alarm
    // records, and (by file version) the attack options. Load empties the mode list first, creates
    // each mode by type (stopping at an unknown type, 9 or above 10) and maps a call radius equal
    // to [Logic] CallForHelpRadiusDefault to CallForHelpRadius.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // The standard mode sets of the script's Guard / Sentry commands: ClearModes, then the guard
    // (radius, wander -1: the default) or sentry mode, Suspection, Aggression, CorpseWatcher and
    // Fear(0); the target is released and the action dropped. 005D6230 / 005D64E0.
    void StandardGuard(const Vector3* point, float radius);
    void StandardSentry(const Vector3* point);
    // New modes handed to AddMode (operator new; a failed allocation adds null).
    void AddGuardMode(const Vector3* point, float radius, float wander);
    void AddSuspectionMode();                       // 005D63B0 (point and level left unset)
    void AddAggressionMode();
    void AddCorpseWatcherMode();
    void AddFearMode(std::uint8_t flag);            // 005D6480 (distance from 0073FF00, 3, unset point)
    void AddSentryMode(const Vector3* point);
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

    // Action 2 (move) after arrival: run the queued looks, each turning then waiting.
    void UpdateMove();
    // Action 3 (attack): give up when the target is dead, no longer an enemy or the
    // controller stopped attacking it.
    void UpdateAttack();
    // Drop the action and its target.
    void StopAction();
    // Action 1: walk to `point`.
    void Walk(const Vector3* point, int run, int unknown);
    // Action 1: turn towards `direction`.
    void TurnTowards(const Vector3* direction, float time);
    // Action 4 (when the spell cannot be cast now): cast `spell` at `point`.
    void CastAt(int spell, const Vector3* point, int run);
    // Action 6: follow `object`.
    void Follow(CMapObject* object);
    // Something was noticed: remember `object` as seen, call allies to it (or to the unit
    // itself when null) unless it is an ally, become alerted in alert mode 1, and remember a unit in
    // the perception memory.
    void SetSeen(CMapObject* object);
    // Action 3 against `object` (run unless the player's order check says otherwise).
    void AttackTarget(CMapObject* object);
    // Action 3 against `object` with the given attack parameters; units of other sides
    // that are not enemies are not attacked unless the unit has a player.
    void AttackWith(CMapObject* object, int how, int unknown);
    // Action 5: use something on `object` (kind 0 a point/object, 1 and 2 units, 0x18A95
    // a special unit use).
    void UseOn(CMapObject* object, int kind, int a, int b);

    // unit_ai_moves.cpp.
    // Action 6 (follow `target`): every few ticks walk to where the leader is or will be
    // (a moving leader is met ahead on its way, or passed on the side); a dead or lost leader ends it.
    void UpdateFollow();
    // Replaces the looks of `order` by one to four random directions (each the first that
    // the AI map sees clear, within 6 tries) around `direction`, each followed by a random wait
    // between a quarter and all of the wander time (40 when negative, else 15 x `wander`). A `far`
    // look (not for busy units nor "unhu*" prototypes) is a single one with a longer wait.
    void PlanLooks(AIMoveOrder* order, const Vector3* direction, float wander, bool far);
    // A random place within `radius` of `point` to stand at: not too steep, no unit of an
    // allied player near it, and a path there not much longer than the way. After a failure the
    // next calls (up to 10, a third of the distance) fail at once.
    bool FindStandPoint(const Vector3* point, float radius, Vector3* out);
    // 005D5990 (unit_ai_brain.cpp). Every allied unit within +0x38 of the owner's unit hears a call
    // for help (kind 0x1A) at `point`; the message text is formatted only while the console shows
    // filter bit 0.
    void CallAllies(const Vector3* point);
    void ChooseTarget(AITargetChoice* choice, void* candidates, const Vector3* from, int a, int b); // 005D0030 on +0xA4
    AIRefTo<CUnit>* PopCorpse(AIRefTo<CUnit>* out);                                     // 005D8D50 on the memory's corpses

    CUnitServer* owner;             // +0x000
    int action;                     // +0x004
    AIMoveOrder move;               // +0x008 the action point, and what to look at after arriving (+0x014)
    int actionTime;                 // +0x028 game time the current look ends (or kNoTime / kLookStarted)
    AIRefTo<CMapObject> target;     // +0x02C
    AIRefTo<CMapObject> seen;       // +0x030
    std::uint8_t alerted;           // +0x034
    std::int8_t alarm;              // +0x035 alarm kind raised when the countdown ends
    std::int8_t alarmSlot;         // +0x036 the world alarm slot of the applied record (ApplyBehaviour)
    std::uint8_t alwaysUpdated;         // +0x037 the world keeps the unit in its update map (the record's flag bit 3)
    float callRadius;               // +0x038 allies within it hear a call for help (10.0 from 0073FED8 at Init)
    int alertMode;                  // +0x03C
    int alarmCountdown;             // +0x040
    float nearX;               // +0x044 near point (Guard); 100000 when cleared
    float nearY;                        // +0x048 near point y (SetPositions); 100000 when cleared
    float decay;                    // +0x04C alertness decay factor
    Vector3 previousPosition;       // +0x050
    Vector3 position;               // +0x05C
    int standCooldown;              // +0x068 FindStandPoint fails while above 0 (one off per call)
    int followTimer;                // +0x06C UpdateFollow acts once it has counted past 8
    mfc::CTypedPtrList<AIMode> modes;   // +0x070 (elements are counted references)
    AIMode* current;                // +0x08C
    AIAlarmRecordArray alarmRecords;    // +0x090
    AIAttention attention;          // +0x0A4
    AIMemory memory;                // +0x0D0 perception memory
};
static_assert(offsetof(AIBrain, move) == 0x08);
static_assert(offsetof(AIBrain, actionTime) == 0x28);
static_assert(offsetof(AIBrain, alertMode) == 0x3C);
static_assert(offsetof(AIBrain, decay) == 0x4C);
static_assert(offsetof(AIBrain, previousPosition) == 0x50);
static_assert(offsetof(AIBrain, position) == 0x5C);
static_assert(offsetof(AIBrain, modes) == 0x70);
static_assert(offsetof(AIBrain, current) == 0x8C);
static_assert(offsetof(AIBrain, alarmRecords) == 0x90);
static_assert(offsetof(AIBrain, memory) == 0xD0);
static_assert(sizeof(AIBrain) == 0x190);
static_assert(offsetof(AIBrain, attention) == 0xA4);

// The script-made modes' constructors and slots (004BA1A0-004BA9B0) are members of the mode classes
// above (script_unit_modes.cpp). Slots of 3 bytes (RET 4, MOV AL,1, XOR EAX,EAX ...) cannot be
// hooked: 004BA220 / 004BA230 / 004BA240 / 004BA2B0 / 004BA2C0 / 004BA2F0 / 004BA620 / 004BA970.
