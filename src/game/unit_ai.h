#pragma once

// Unit AI: the per-unit brain that picks a behaviour mode every AI tick and runs it, the modes (Guard, Patrol, Sentry,
// Suspection, Aggression, CorpseWatcher, Player, Fear, Tamed, Follow; the script's UM* commands create them) and
// the actions the brain issues. The controller embeds the brain and calls AIBrain::Think once per AI update. Modes
// are reference-counted and kept in the brain's list; the brain holds a counted pointer to the mode in charge.

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
class CUnitServer;
struct AIEventPresence;

struct CountedUnits : mfc::CTypedPtrList<CUnit> {
    // Appends `object`, holding a reference; returns the node.
    Node* AddCountedTail(CUnit* object);
    // The brain's corpse list's head handed to `out` with a reference (the list's own reference released). Returns `out`.
    AIRefTo<CUnit>* PopHead(AIRefTo<CUnit>* out);
};
using AIObjectList = CountedUnits;


// An order as the controller's order checks take it (built on the stack).
struct AIOrder {
    // Kind 9 (none): the object released and cleared, the flag cleared.
    void Clear();
    // Clear, then an attack (3, 6) on `object` (referenced).
    void MakeAttack(CMapObject* object);
    // Clear, then a cast (5) of `spell` at `object` (referenced).
    void MakeCast(int spell, CMapObject* object);
    // Clear, then a use (6) of `object` with `what`.
    void MakeUse(CMapObject* object, int what);
    // Where the order aims: the object's AimPoint, or `point` without an object. Returns `out`.
    Vector3* AimPoint(Vector3* out) const;

    int kind;                       // 3 attack, 4/5 cast (spell or quick item), 6 use/skill, 9 none
    std::uint32_t source;           // 6 for an attack, the spell for a cast; in a UnitAction
                                    //       (combat.h) the spell slot, or quick item slot | 0x80000000;
                                    //       0x18A95 for a skill used on a target (kind 6)
    AIRefTo<CMapObject> target;     // counted
    Vector3 point;                  // target point; left uninitialised by the AI's orders
    std::uint8_t flag18;
    std::uint8_t unused_19[3];
};
static_assert(sizeof(AIOrder) == 0x1C);

// One remembered look direction of the current move.
struct AILook {
    float angle;
    float time;                     // turning time
    float wait;                     // wait afterwards (game ticks)
    std::uint8_t stopFirst;
    std::uint8_t unused_0D[3];

    // The arguments come in a different order from the fields: wait before turning time. Returns this.
    AILook* ScriptAILookConstruct(float newAngle, float newWait, float newTime, int newStopFirst);
};
static_assert(sizeof(AILook) == 0x10);

// CArray<AILook>; new elements are zeroed.
struct AILooks : mfc::CArray<AILook> {
    // The brain's look queue and the patrol looks of UMPatrol. Grown to index + 1 when needed (new looks zeroed).
    void SetAtGrow(int index, const AILook& look);
    AILook* CopyAt(AILook* out, int index) const;
};
using AILookArray = AILooks;
void __fastcall LooksDestroyAt(AILookArray* looks);
AILookArray* __fastcall LooksInit(AILookArray* looks);
// The looks from / to a save: the
// count, then angle, time and wait of each and (files from version 0x44) the stop flag.
void __fastcall LoadLooks(AILookArray* looks, CStream* stream);
void __fastcall SaveLooks(const AILookArray* looks, CStream* stream);

// A move the modes prepare on the stack: where to go and what to look at there.
struct AIMoveOrder {
    void Clear();
    AIMoveOrder* Assign(const AIMoveOrder* other);

    Vector3 point;
    AILookArray looks;
};
static_assert(sizeof(AIMoveOrder) == 0x20);

// Mode types, as the brain names them in its "new motivation" message.
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

// Base of all modes (abstract). AIActionMode is the base of the modes that act.
class AIMode : public CRefObject {
public:
    AIMode();
    ~AIMode() override;

    // The priority the mode claims this tick; 50 here (Guard, Sentry, Follow).
    virtual int Evaluate(AIBrain* brain);
    // Runs while the mode is in charge. Here: the idle remark (1 in 1001), then LookAround(0).
    virtual void Execute(AIBrain* brain);
    // Entering (Guard, Patrol, Sentry, Follow): drop the action and target, pick a new alertness decay (0.8 + 0.4 * random) and clear the controller's busy flag.
    virtual void Enter(AIBrain* brain);
    virtual void Leave(AIBrain*) {}
    virtual int GetType() = 0;
    // True for most modes; AddMode replaces a mode of another type when both answer true.
    virtual bool Exclusive() { return true; }
    virtual void OnObjectDeleted(CEngineObject*) {}
    virtual void Load(CStream*) {}
    virtual void Save(CStream*) {}
};
static_assert(sizeof(AIMode) == 8);

// Base of the modes that act: Suspection, Aggression, CorpseWatcher, Fear and Player.
class AIActionMode : public AIMode {
public:
    AIActionMode();
    ~AIActionMode() override;
    int Evaluate(AIBrain* brain) override = 0;
    void Execute(AIBrain* brain) override = 0;
    void Enter(AIBrain* brain) override;
    bool Exclusive() override { return false; }
};

// Guard: stay near a point.
class GuardMode : public AIMode {
public:
    GuardMode() { failures = 0; }
    GuardMode(const Vector3* point, float radius, float wander);
    ~GuardMode() override;
    int OnShutdown() override;
    GuardMode* Clone() override;
    // Sets the point and the two radii (negative: the globals).
    void Set(const Vector3* point, float radius, float wander);
    // When idle: walk to a free place near the point (after 100 failures, near
    // the unit) and look around there.
    void Execute(AIBrain* brain) override;
    int GetType() override { return kModeGuard; }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 point;
    float radius;
    float wander;
    int failures;
};
static_assert(sizeof(GuardMode) == 0x20);

// One patrol point: position and a CArray of look / wait entries.
struct PatrolPoint {
    // The element assignment of the point array's Add / SetAtGrow: the position, then the looks' SetSize to the source's count and each look copied.
    PatrolPoint& operator=(const PatrolPoint& other);

    Vector3 position;
    AILookArray looks;
};
static_assert(sizeof(PatrolPoint) == 0x20);

// The point array's ConstructElements (zeroed, each look array given) and DestructElements (each look array freed).
struct PatrolPointElements {
    static void Construct(PatrolPoint* points, int count);
    static void Destruct(PatrolPoint* points, int count);
};

// CArray<PatrolPoint>.
struct PatrolPoints : mfc::CArray<PatrolPoint, PatrolPointElements> {
    void Destruct();
    PatrolPoints* DeletingDestruct(int flags);
    PatrolPoint* CopyAt(PatrolPoint* out, int index) const;
};
using PatrolPointArray = PatrolPoints;

// Patrol: walk a list of points.
class PatrolMode : public AIMode {
public:
    PatrolMode();
    ~PatrolMode() override;
    int OnShutdown() override;
    PatrolMode* Clone() override;
    // 50; without points: FatalError "Unit %i has no patrol points, switching to
    // guard", a point at the unit's position, and -1000.
    int Evaluate(AIBrain* brain) override;
    // When idle: on to the next point and its looks.
    void Execute(AIBrain* brain) override;
    int GetType() override;
    PatrolPoint* LastPoint();
    // The point count, per point its position, look count and per look angle, time, wait and (from version 0x44 when loading) the stop flag byte; then `current`.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    void AddPoint(const Vector3* position);
    // Appends a point with no looks at ((n + 0.5) / 2, z 0) for
    // n = 2x rounded to nearest (so 0..0.5 above `position`, not the middle of its half-cell).
    void AppendPoint(const Vector3* position);

    PatrolPointArray points;
    int current;
};
static_assert(sizeof(PatrolMode) == 0x20);

// Sentry: stand at a point.
class SentryMode : public AIMode {
public:
    SentryMode() {}   // a mode AIBrain::Load fills
    explicit SentryMode(const Vector3* point);
    ~SentryMode() override;
    int OnShutdown() override;
    SentryMode* Clone() override;
    void Set(const Vector3* point);
    void Execute(AIBrain* brain) override;
    int GetType() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 point;
};
static_assert(sizeof(SentryMode) == 0x14);

// Follow: stay with a leader.
class FollowMode : public AIMode {
public:
    explicit FollowMode(CMapObject* leader = nullptr);
    ~FollowMode() override;
    int OnShutdown() override;
    FollowMode* Clone() override;
    void Execute(AIBrain* brain) override;
    int GetType() override;
    void OnObjectDeleted(CEngineObject* object) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> leader;
};
static_assert(sizeof(FollowMode) == 0x0C);

// Suspection: go and look where something was noticed.
class SuspectionMode : public AIActionMode {
public:
    SuspectionMode() { ClearFields(); }
    SuspectionMode* Construct();
    ~SuspectionMode() override;
    int OnShutdown() override;
    SuspectionMode* Clone() override;
    // Remembers a non-allied seen object, then takes over the brain's noticed
    // point and level (the level only grows down to the noticed one while the point is the same).
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain* brain) override;
    // Busy; walk to the point; remark 5.
    void Enter(AIBrain* brain) override;
    // Clears busy; remark 3.
    void Leave(AIBrain* brain) override;
    int GetType() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // game.exe's constructor leaves these unset, but Evaluate keeps a point that compares the same (a NaN always does)
    // and the lower level, so every new mode starts zeroed: a determinism fix, the harness zeroes game.exe's alike
    // (tests/verify/harness_leftovers.h).
    void ClearFields() {
        point = {};
        level = 0;
    }

    Vector3 point;
    int level;
};
static_assert(sizeof(SuspectionMode) == 0x18);

// A target the attention chose or the brain is attacking: the part (-1 the whole unit with a weapon,
// else the spell's part) and the object.
struct AITargetChoice {
    int part;
    CUnit* object;
};
static_assert(sizeof(AITargetChoice) == 8 && offsetof(AITargetChoice, object) == 4);

// Aggression: fight an enemy.
class AggressionMode : public AIActionMode {
public:
    // No enemy, the whole unit as the part to hit; `fixed` (UMRevenge) says the script set the enemy.
    AggressionMode();
    explicit AggressionMode(std::uint8_t fixed);
    ~AggressionMode() override;
    int OnShutdown() override;
    AggressionMode* Clone() override;
    // 10000 while there is an enemy to fight (the script's, or the best one
    // in sight), else -1000.
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain* brain) override;
    // Busy; drops action and target; remark 6.
    void Enter(AIBrain* brain) override;
    // Forgets the noticed level when the enemy is gone or dead, drops the enemy,
    // clears busy; remark 4.
    void Leave(AIBrain* brain) override;
    int GetType() override;
    void OnObjectDeleted(CEngineObject* object) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> enemy;
    std::uint8_t fixed;             // enemy set by the script (no searching)
    std::uint8_t unused_0D[3];
    AITargetChoice chosen;          // the enemy chosen this tick; part -1: the whole unit
};
static_assert(sizeof(AggressionMode) == 0x18);

// CorpseWatcher (8 bytes): reports corpses the brain has seen (its Load and Save are the base's empty ones).
class CorpseWatcherMode : public AIActionMode {
public:
    CorpseWatcherMode();
    ~CorpseWatcherMode() override;
    int OnShutdown() override;
    CorpseWatcherMode* Clone() override;
    // Takes one seen corpse off the brain's list and notes it as noticed
    // (level 800, kind -3). Never takes charge (-1000).
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain*) override {}
    int GetType() override;
};

// Fear: run from an enemy.
class FearMode : public AIActionMode {
public:
    // Every field is zeroed first (ClearFields; a determinism fix: game.exe leaves shelter y / z and fleePoint x / y, which
    // the saves write, and the harness zeroes game.exe's alike), then distance, spread 3 and an unset shelter (x = -1313).
    explicit FearMode(std::uint8_t flag);
    FearMode* Construct(std::uint8_t flag);
    void ClearFields() { std::memset(&distance, 0, sizeof(FearMode) - offsetof(FearMode, distance)); }
    ~FearMode() override;
    int OnShutdown() override;
    FearMode* Clone() override;
    int GetType() override;
    // Remembers a seen object and calms `spread`; 15000 for a ranged unit that can run, 3000 while it sees an enemy, 350 in
    // shelter, else noticedLevel / 6 or -1000. See docs/notes/unit_ai.md, "FearMode::Evaluate".
    int Evaluate(AIBrain* brain) override;
    // Runs from the nearest enemy (calling allies to it), else
    // stays in shelter, else from the noticed point (forgotten unless `flag` and above 500): within
    // `distance`, and not walking on command, to `fleePoint` when one is pending, else 5 away plus
    // a random offset of at most `spread` (which first drops to 2.5 x `distance`), when a path is
    // found (action 1); `spread` doubles. Without an action: attack the best target in sight
    // (allies only), else stand and look away from the point.
    void Execute(AIBrain* brain) override;
    void Enter(AIBrain* brain) override;
    void Leave(AIBrain* brain) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    float distance;                 // how near danger counts (10, FearDefaultDistance)
    float spread;                   // random spread of a flight point (3)
    Vector3 shelter;                // the last place with a kind 3 occluder (x = -1313: unset)
    Vector3 fleePoint;              // a checked flight point (z 1.0 while pending, else 0)
    std::uint8_t flag;
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

// Player: the party's units.
class PlayerMode : public AIActionMode {
public:
    enum { kStateFollow = 0, kStateCommand = 1, kStateMoveTo = 2 };

    PlayerMode();
    ~PlayerMode() override;
    int OnShutdown() override;
    PlayerMode* Clone() override;
    int Evaluate(AIBrain* brain) override;
    int GetType() override;

    // State 0: follow the leader (unless the leader is moving itself) and fight
    // state 1: wait for the commanded action to end; state 2: walk to `point` (staying
    // with the party unless the player controls the unit directly) and fight on the way.
    void Execute(AIBrain* brain) override;
    void OnObjectDeleted(CEngineObject* object) override;
    // The target's and the leader's
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
    void CommandAttack(AIBrain* brain, CMapObject* object, int how, int unknown);
    void CommandUse(AIBrain* brain, CMapObject* object, int kind, int a, int b);
    void CommandCast(AIBrain* brain, int spell, CMapObject* object, int unused);
    void CommandCastAt(AIBrain* brain, int spell, const Vector3* point, int unused);

    // Follow `leader` (counted): none when it is the unit itself. When the chain
    // of leaders from `leader` (each unit's first Player mode) comes back to this unit, this unit's
    // own Player mode stops following first. A walk in progress (action 1) is dropped (state 0).
    void SetLeader(AIBrain* brain, CUnit* leader);

    // Only for units whose +0x438 is 1: the mode's target when it can be reached
    // from here (attacked at once); else the best target (no allies) among the enemies
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
    // False when a unit of the player whose (first) Player mode follows
    // this unit is farther than [Logic] FollowMaxDist x (2 while `moving`, else 1.4) + 1 (3D).
    bool StaysWithParty(AIBrain* brain, bool moving);
    // Can the unit get from `from` to `to`: false when they are within
    // 0.1 (2D), else a path search with the unit moved to `from` for it (range twice the distance).
    bool PathClear(AIBrain* brain, const Vector3* from, const Vector3* to);

    AIRefTo<CMapObject> target;
    AIRefTo<CUnit> leader;
    Vector3 point;                  // (z: 1.0 while walking there in state 2)
    int state;
    std::uint8_t run;
    std::uint8_t unused_21[3];

protected:
    // Both OnShutdowns: the references released, then the constructor's stores.
    void Reset();
};
static_assert(sizeof(PlayerMode) == 0x24);

// Tamed (type 8): the Player mode a taming installs (combat_taming.cpp).
class TamedMode : public PlayerMode {
public:
    TamedMode() {}
    ~TamedMode() override;
    int OnShutdown() override;
    TamedMode* Clone() override;
    int Evaluate(AIBrain* brain) override;   // always 100000000
    int GetType() override;
};


// A count read from `stream`, then that many counted
// references appended to `list`, each loaded in `context` (the trap's units, the brain's lists).
void __fastcall CountedListLoad(mfc::CTypedPtrList<CUnit>* list, CStream* stream, CWorld* context);

class CSpell;
namespace console {
class GameOStream;
}
console::GameOStream* AiLog();

// Copies x and y of a point (the AI events'
// and the heard calls' noticed points); returns `out`.
float* __fastcall CopyPoint2(float* out, const float* point);

// A call the perception memory heard (CallAllies sends kind 0x1A).
struct AICallRecord {
    AICallRecord* DeleteScalar(unsigned flags);
    Vector3 point;
    int kind;
    int hostileMask;                    // sides the hearer turns hostile to (0 for a plain call)
    mfc::CString text;

    void Destruct();
    AICallRecord& operator=(const AICallRecord& other);
};
static_assert(sizeof(AICallRecord) == 0x18 && offsetof(AICallRecord, text) == 0x14);

struct AICallElements {
    static void Construct(AICallRecord* calls, int count);
    static void Destruct(AICallRecord* calls, int count);
};

struct AIMemory;

struct AIHeardCalls : mfc::CArray<AICallRecord, AICallElements> {
    // Counts every call
    // down; one that runs out is dropped after it is heard: with a hostility mask the
    // unit's controller takes the mask (logged "hear delayed sound"), else, when the memory's
    // noticed level is below 900, the call's point becomes the noticed point (900, kind -3; logged
    // when the level rose by more than 100 or the point lies more than sqrt(10) away).
    void Update(CUnit* unit, AIMemory* memory);
    // The calls without a mask whose kind is `kind` or above are dropped, then {point, kind, 0, text} is appended.
    void Hear(const Vector3* point, int kind, const char* text);
    // Appends {point not set, kind, mask, text}: when it
    // runs out (Update) the unit's controller takes the hostility mask.
    void AddClaim(std::uint32_t mask, int kind, const char* text);
    void RemoveClaims();
};

// A unit's alarm / assembly record: the map spec's behaviour record (read and written).
using AIAlarmRecord = mapspec::Behaviour;
struct AIAlarmRecordElements {
    static void Construct(AIAlarmRecord* records, int count);
    static void Destruct(AIAlarmRecord* records, int count);
};
using AIAlarmRecordArray = mfc::CArray<AIAlarmRecord, AIAlarmRecordElements>;
// An object the memory reported (the object is a counted reference).
struct AIReport {
    AIRefTo<CMapObject> object;
    int what;
};
static_assert(sizeof(AIReport) == 8);

// The reports' ConstructElements (zeroed, each object null) and DestructElements (each object released).
struct AIReportElements {
    static void __stdcall Construct(AIReport* reports, int count);
    static void Destruct(AIReport* reports, int count);
};

// CArray<AIReport> (the perception memory's reports).
struct AIReports : mfc::CArray<AIReport, AIReportElements> {
    // Keeps the larger `what` of a listed object, else appends it (with a reference).
    void Report(CMapObject* object, int what);
    bool Contains(CEngineObject* object) const;
};

// One way the unit can attack or help: a weapon (part -1) or a spell.
struct AIAttackOption {
    // How much the option counts: kind 0 its largest damage,
    // 1..3 that * 1.3, 4..6 1, 7 10, other kinds 0.
    double Weight() const;
    // The option from / to a save. Before version 0x5A only the part, a dropped dword and the damage were
    // saved: the kind becomes part >= 0, the range 1, the rest 0.
    void Load(CStream* stream);
    void Save(CStream* stream) const;

    int kind;                       // 0..3 attacks (2, 3 hit an area), 4..6 helping spells, 7 healing
    int part;                       // -1 a weapon, else the spell
    float range;
    int maxCount;                   // cap on the group-size bonus
    float radius;                   // area of kinds 2 and 3
    int effect;                     // effect type (kinds 4..6), strength (kind 3)
    std::uint16_t damage[8];        // measured against the target's protection
};
static_assert(sizeof(AIAttackOption) == 0x28);

// CArray<AIAttackOption>; elements are zero-filled.
using AIAttackOptions = mfc::CArray<AIAttackOption>;
static_assert(sizeof(AIAttackOptions) == 0x14);
// The count, the array sized to it, then each option.
void __fastcall LoadAttackOptions(AIAttackOptions* options, CStream* stream);


// The brain's attention: the unit's attack options and choosing a target (unit_ai_targets.cpp).
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
    // How good `option` is against `candidate` (`group` the
    // others it would hit or help): by kind, minus 25 per unit of distance beyond the range (x10
    // outside kinds 0..4) when it reached `threshold`, minus twice the hits needed for attacks.
    // -1e30 when it cannot apply. Returned unrounded.
    double Score(const Vector3* from, CUnit* self, AIObjectList* group, CEngineObject* current, CUnit* candidate,
                 float threshold, const AIAttackOption* option);
    // Whether the unit may attack / cast at the target: false for a
    // spell aimed at no object against a large unit of size class 0; else the order is checked
    // from the unit's place when `fromHere` (low byte) is set or the target is the
    // unit itself, otherwise from the end of a path search to the target (range ScaledValue of the
    // distance, the target lifted off the sector map meanwhile), or the unit's place without one.
    bool CanReach(AIBrain* brain, const AITargetChoice* choice, int fromHere);

    // Chooses the best target in sight (Choose from the
    // unit's position) and, when it differs from the current attack, attacks it (action 3; the
    // player's order check may lift the flags to -1) or casts spell `part` at it (action 4 when the
    // spell cannot be cast now).
    void LookAround(AIBrain* brain, int unused);
    // The brain's current attack as {part, object}: action 3
    // {-1, target}, action 4 {spell, target}, the object only when it is a unit; otherwise only the
    // object is cleared.
    void CurrentAttack(AIBrain* brain, AITargetChoice* choice);
    // Takes up {part, object} unless there is no object or it is
    // the current attack: part -1 AttackTarget, else cast it (action 4 when the spell cannot be cast
    // now; target and actionTime = the spell).
    void ApplyAttack(AIBrain* brain, const AITargetChoice* choice);

    const AIAttackOption* FindOption(int part) const;

    // Lists the unit's options: its weapon (unless its natural weapon is 0x10,
    // "spells only") and attack spells (kinds 1..4) into `weapons`, helping spells (5..7) into
    // `spells`; a unit whose id is a text key gets only the weapon. Then `healWeight` is the
    // largest Weight of the `weapons` options.
    void Build(CUnit* unit);
    void InitOption(AIAttackOption* option, int kind, int part, float range, int maxCount, float radius, int effect);
    std::uint16_t* AddWeaponOption(int kind, int part, float range, int maxCount, float radius, int effect);
    std::uint16_t* AddSpellOption(int kind, int part, float range, int maxCount, float radius, int effect);
    // The option kind of a spell by its type (high word of +0x18):
    // 1..7, 8 for none; 0 (EAX = the null pointer) without a spell.
    int SpellKind(const CSpell* spell);

    AIAttackOptions weapons;
    AIAttackOptions spells;
    float healWeight;               // the largest Weight of the weapons (Build); kind 7 scores with it
};
static_assert(sizeof(AIAttention) == 0x2C);

// The AI's eye (the world server's, only the leading part): the unit being updated's position, eye and facing.
struct AIViewer {
    // The viewer moves to the owner's unit, the
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
    // A noise of
    // `loudness` made by `source`'s unit at `at` (null: the unit's position): every unit within 50
    // of the point (the sector map) that is hostile to the source's unit, whose noticed level is
    // below 900 and whose hearing (moveFactors[1] x valuesB0[3]) times the loudness reaches past
    // the distance hears a call of `kind` at the source unit's position (AIHeardCalls::Hear; the text
    // "Unit %i heard unit %i from %fm distance" only with console filter bit 0).
    void Noise(CUnitServer* source, float loudness, int kind, const Vector3* at);
    // The noise of a moving unit: Noise of kind 0x1A at the unit, as loud as moveFactors[0] x
    // valuesE0[3] x the noise of the ground at `position` (the AI map's terrain type). Returns 0
    // (no presence record; the controller keeps the result at +0x918).
    AIEventPresence* StepNoise(const Vector3* position, CUnitServer* source);

    CAIMap* map;
    int tick;                       // the world's event table: its tick (AIEventTable::Advance)
    Vector3 eye;
    Vector3 facing;
    int cellX;                      // AI map cell of the eye
    int cellY;
    std::uint8_t ready;             // Prepare has run
    std::uint8_t unused_29[3];
};
static_assert(offsetof(AIViewer, facing) == 0x14);

// The cells `seer` sees as `observer` would be seen, into `grid` (cleared to one cell first): within
// sight (the two units' sight values, at least 0) inside the cone, up to the observer's mode
// height; within hearing all around; within the peripheral range in the front half.
void __fastcall MarkSightCells(CUnit* seer, CUnit* observer, FogGrid* grid);
static_assert(offsetof(AIViewer, ready) == 0x28);

// An AI event: filed in its map cell's list for its side and in the time wheel's slot
// of the tick it expires. Records come from the pool CList (AIEventPool) and go back to
// the free list (chained through `next`).
struct AIEvent {
    float x, y;                     // where; its cell is (x / 8, y / 8) rounded
    std::uint32_t origin[2];            // x and y again (sets them; saved)
    int expiry;                     // the tick it goes; wheel slot (expiry - 1) & 0x3FF
    std::uint32_t payload[2];           // saved and loaded; left to the creator's caller
    int sourceId;                   // the unit that made it
    int side;                       // its low 4 bits: the side list it is filed in
    AIEvent* next;                  // the cell's list for the side; the free list's link
    AIEvent* prev;                  // null for the cell's first
    AIEvent* nextInSlot;            // the wheel slot's list
};
static_assert(sizeof(AIEvent) == 0x30);
// CList<AIEvent>'s destructor (only the exit function calls it, on the pool): its vtable back, RemoveAll.
void __fastcall AIEventPoolDestruct(mfc::CList<AIEvent>* pool);

// A map cell of the event grid: the newest event of each side (lists through AIEvent::next).
struct AIEventCell {
    AIEvent* bySide[16];
};

class CStream;

// The free event records, chained through AIEvent::next.
extern AIEvent* g_freeEvents;

// The world's AI events (its viewer part makes the noises): a time wheel of 1024 ticks x 16 sides and a grid of 8 x 8
// cells (rows of width cells). Its tick is the viewer's `tick` (Advance sets it).
struct AIEventTable : AIViewer {
    // The wheel moves to `tick`: every event of that tick's 16 slots leaves its cell list and goes to the
    // free list; the slots empty. A first event whose cell is outside the grid faults (a null cell is read).
    void Advance(int tick);
    // Drops every event made by `controller`'s unit in the wheel column of its side (unit side & 15), as Advance drops them.
    void ForgetUnit(CUnitServer* controller);
    // (the save). Load: while a byte 1 precedes, an event (its fields
    // as Save writes them) filed in its cell and wheel slot, or freed when outside the grid. Save:
    // each cell's events by side (byte 1, x / y, +0x08, expiry, +0x14, source, side), then byte 0.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // A 1 x 1 grid, then Clear. Returns this.
    AIEventTable* Construct();
    // Tick -1, the wheel emptied (its events are not freed), the grid back to 1 x 1
    // (reallocated unless it is) and its cell emptied.
    void Clear();
    // Every event of the wheel goes to the free list; the grid is freed.
    void Destroy();
    // The table for a map of sizeX x sizeY: Clear, the map kept, the grid 1 - trunc(size * -0.125)
    // cells each way (reallocated unless it has that size) and emptied. Then the 4096 bytes of
    // "normal.dat" in the text resources' "aiinfo.res" are copied. Returns 1; a
    // missing archive or file is a fatal error, whose result is returned.
    int Setup(CAIMap* aiMap, float sizeX, float sizeY);

    AIEventCell* CellOf(const AIEvent* event) const;
    void Free(AIEvent* event, int side);

    AIEvent* slots[0x400][16];      // by tick & 0x3FF, then side
    AIEventCell* cells;             // the grid's block
    AIEventCell** rows;             // the grid rows (each `width` cells)
    int width;
    int height;
};
static_assert(offsetof(AIEventTable, slots) == 0x2C);
static_assert(offsetof(AIEventTable, rows) == 0x10030);
static_assert(sizeof(AIEventTable) == 0x1003C);

double __fastcall AIDistance2(const Vector3* a, const Vector3* b);

// The brain's perception memory: what the unit sees, has seen and heard.
struct AIMemory {
    void Reset();
    void Destruct();
    // Ages the reports (one tick off each; an entry below 0 is
    // dropped), updates the heard calls and the AI events' view,
    // then adds the noticed kind to the noticed level. Both callees may set the noticed point, level
    // and kind inline first (900 / -3, 800 / -3, 1000 / -2).
    void Update(CUnitServer* owner);
    // 1 when `other` has the unit's side or player or is in
    // the visible list, else 0.
    int IsEnemy(CUnit* self, CEngineObject* other);
    // Takes over a noticed point of a higher `level`: 1 when
    // the level rose by more than 100 or the point lies more than sqrt(10) away (with the point's
    // own z, not a difference), else 0; 0 when the level is not higher.
    int Notice(const Vector3* point, int level, int kind);
    void Forget(CEngineObject* object);
    void HearCall(const Vector3* point, int kind, const char* text);
    void AddClaim(std::uint32_t mask, int kind, const char* text);
    // `out` emptied, then the visible units hostile to the
    // unit (its IsHostileTo), alive and not reported, in visible-list order (no references taken).
    void FindEnemies(CUnit* self, AIObjectList* out);
    // The same with the visible unit's own IsHostileTo(self); one at or beyond
    // `radius` of `point` (3D; all of them for a radius <= 0) only when it has an aggression mode
    // (not a fixed one without an enemy) whose enemy is none, not a unit, or a unit of a side the
    // unit's side is allied with, and its brain sees an enemy.
    void FindEnemiesNear(CUnit* self, AIObjectList* out, const Vector3* point, float radius);
    CUnit* NearestEnemy(CUnit* self);
    // Forces `object` into sight when it is not visible and attacks the unit or
    // stands at or beyond one of the two sight ranges: a corpse joins the seen corpses (and the
    // corpses when allied), a living unit the visible list's head (both counted); each logged.
    void ForceSee(CUnit* self, CUnit* object);
    void SeeCorpse(CUnit* self, CUnit* corpse);
    // The unit's exposure to the living hostile units nearby
    // into `out` (5 floats, CUnit + 0x4C8): per kind the smallest distance / range ratio, at most 3
    // (a zero range counts 3). [0] sight (scaled by the line of sight between the eyes, only within
    // range), [4], [1], [2] three other ranges; [3] stays 3.
    void MeasureExposure(CUnitServer* self, float* out);
    // From / to a save: the four object lists (appended to on load), the noticed point, level and countdown,
    // the reports, the heard calls (point, kind, text, +0x10) and the noticed kind (-1 until read).
    void Load(CStream* stream);
    void Save(CStream* stream);

    SpiralCache spareCells;         // set empty by the brain's constructor; not read
    SpiralCache sightCells;         // the cells around the unit it looks for units in
    float noticedX;
    float noticedY;
    int noticedLevel;
    int noticedKind;
    int nearbyCountdown;            // updates until the nearby units are listed again
    AIObjectList nearby;            // the units in the sight cells around (counted references)
    AIObjectList visible;           // units in sight
    AIObjectList seenCorpses;       // corpses seen (counted references)
    AIObjectList corpses;           // allied corpses seen (counted references)
    AIReports reports;          // objects reported to the alarm code
    AIHeardCalls calls;              // calls heard
    std::uint8_t seesEnemy;
    std::uint8_t unused_BD[3];
};
static_assert(offsetof(AIMemory, noticedX) == 0x10);
static_assert(offsetof(AIMemory, nearby) == 0x24);
static_assert(offsetof(AIMemory, reports) == 0x94);
static_assert(offsetof(AIMemory, calls) == 0xA8);
static_assert(offsetof(AIMemory, seesEnemy) == 0xBC);
static_assert(sizeof(AIMemory) == 0xC0);

// The brain, embedded in the unit controller.
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

    AIMode* FindMode(int type);
    PatrolMode* FindPatrolMode();
    PlayerMode* FindPlayerMode();   // (the Tamed mode is a Player mode too)
    CUnit* PlayerModeLeader();
    CMapObject* LeaderOrFollowed();
    CMapObject* AggressionEnemy();
    bool IsNearPath(const Vector3* point, float radius);
    // Sets the owner; on a change both remembered positions become "unset" (-1313, 0, 0).
    std::uint32_t SetOwner(CUnitServer* newOwner);
    // An object is being deleted: tell every mode, drop it as target or seen object and
    // from the perception memory.
    void OnObjectDeleted(CEngineObject* object);

    // Adds `mode` (a counted reference, RefPtr::Share) at the end of the list after
    // removing the first mode of the same type, or the first exclusive one when `mode` is exclusive
    // (the mode in charge loses its reference when it is the one removed).
    void AddMode(AIMode* mode);
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
    // The constructor (CUnitServer's, on +0x980): empty containers with their vtables, the move order
    // cleared, both sight caches {0, -1}, then Init. Returns this.
    AIBrain* Construct();
    // Init, the memory destroyed, then the attention arrays, alarm records, the mode in charge, the mode list, target / seen and the looks.
    void Destruct();
    // The brain from / to a save: the action, its point, looks and time, target and seen object, the scalars,
    // the modes (count, the index of the one in charge, then each mode's type and its own data), the
    // perception memory, the alarm records, and (by file version) the attack options. Load empties the mode
    // list first, creates each mode by type (stopping at an unknown type, 9 or above 10) and maps a call
    // radius equal to [Logic] CallForHelpRadiusDefault to CallForHelpRadius.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // The standard mode sets of the script's Guard / Sentry commands: ClearModes, then the guard (radius, wander -1: the default) or sentry mode, Suspection, Aggression, CorpseWatcher and Fear(0); the target is released and the action dropped.
    void StandardGuard(const Vector3* point, float radius);
    void StandardSentry(const Vector3* point);
    // New modes handed to AddMode (operator new; a failed allocation adds null).
    void AddGuardMode(const Vector3* point, float radius, float wander);
    void AddSuspectionMode();                       // (point and level left unset)
    void AddAggressionMode();
    void AddCorpseWatcherMode();
    void AddFearMode(std::uint8_t flag);            // (distance, 3, unset point)
    void AddSentryMode(const Vector3* point);
    // The modes of behaviour `record` (type 1 guards, 5 the world alarm point, 3 a sentry, 2 patrols, 4 the party's Player mode,
    // plus the reaction's modes), then the alarm, no action or target, the attack options rebuilt. See docs/notes/unit_ai.md,
    // "AIBrain::ApplyBehaviour".
    void ApplyBehaviour(const AIAlarmRecord* record, int slot);
    // The records copied into `alarmRecords`, then the first applied.
    void SetAlarmRecords(const AIAlarmRecord* records, int count);
    // The world alarm `slot` was raised: the
    // behaviour record of that slot is applied (the first always, the others only with their
    // +0x38 set) and the owner raises its own alarm to 7. Nothing for a slot without a record.
    void HearAlarm(int slot);
    // The perception memory emptied, then the behaviour
    // record of the slot last applied (unknown_36) applied again.
    void ResetBehaviour();

    // Action 2 (move) after arrival: run the queued looks, each turning then waiting.
    void UpdateMove();
    // Action 3 (attack): give up when the target is dead, no longer an enemy or the
    // controller stopped attacking it.
    void UpdateAttack();
    void StopAction();
    void Walk(const Vector3* point, int run, int unknown);
    void TurnTowards(const Vector3* direction, float time);
    // Action 4 (when the spell cannot be cast now): cast `spell` at `point`.
    void CastAt(int spell, const Vector3* point, int run);
    void Follow(CMapObject* object);
    // Something was noticed: remember `object` as seen, call allies to it (or to the unit
    // itself when null) unless it is an ally, become alerted in alert mode 1, and remember a unit in
    // the perception memory.
    void SetSeen(CMapObject* object);
    void AttackTarget(CMapObject* object);
    // Action 3 against `object` with the given attack parameters; units of other sides
    // that are not enemies are not attacked unless the unit has a player.
    void AttackWith(CMapObject* object, int how, int unknown);
    // Action 5: use something on `object` (kind 0 a point/object, 1 and 2 units, 0x18A95
    // a special unit use).
    void UseOn(CMapObject* object, int kind, int a, int b);

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
    // Every allied unit within +0x38 of the owner's unit hears a call
    // for help (kind 0x1A) at `point`; the message text is formatted only while the console shows
    // filter bit 0.
    void CallAllies(const Vector3* point);
    void ChooseTarget(AITargetChoice* choice, void* candidates, const Vector3* from, int a, int b);
    AIRefTo<CUnit>* PopCorpse(AIRefTo<CUnit>* out);

    CUnitServer* owner;
    int action;
    AIMoveOrder move;               // the action point, and what to look at after arriving
    int actionTime;                 // game time the current look ends (or kNoTime / kLookStarted)
    AIRefTo<CMapObject> target;
    AIRefTo<CMapObject> seen;
    std::uint8_t alerted;
    std::int8_t alarm;              // alarm kind raised when the countdown ends
    std::int8_t alarmSlot;         // the world alarm slot of the applied record (ApplyBehaviour)
    std::uint8_t alwaysUpdated;         // the world keeps the unit in its update map (the record's flag bit 3)
    float callRadius;               // allies within it hear a call for help (10.0 at Init)
    int alertMode;
    int alarmCountdown;
    float nearX;               // near point (Guard); 100000 when cleared
    float nearY;                        // near point y (SetPositions); 100000 when cleared
    float decay;                    // alertness decay factor
    Vector3 previousPosition;
    Vector3 position;
    int standCooldown;              // FindStandPoint fails while above 0 (one off per call)
    int followTimer;                // UpdateFollow acts once it has counted past 8
    mfc::CTypedPtrList<AIMode> modes;   // (elements are counted references)
    AIMode* current;
    AIAlarmRecordArray alarmRecords;
    AIAttention attention;
    AIMemory memory;                // perception memory
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

