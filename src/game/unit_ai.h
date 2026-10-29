#pragma once

// Unit AI: the per-unit brain that picks a behaviour mode every AI tick and runs it, the modes (Guard, Patrol, Sentry,
// Suspection, Aggression, CorpseWatcher, Player, Fear, Tamed, Follow; the script's UM* commands create them) and
// the actions the brain issues. The controller embeds the brain and calls AIBrain::Think once per AI update. The
// brain owns its modes (one list) and points at the one in charge.

#include "ai_alarm.h"
#include "ai_event_table.h"
#include "ai_map.h"
#include "core_refcount.h"
#include "engine_object.h"
#include "map_spec.h"
#include "maths.h"
#include "unit.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

struct AIBrain;
class CSpell;
class CUnitServer;
namespace console {
class Stream;
extern Stream g_aiLogOut;
}

// A list of units holding no references (candidates, parties).
using UnitList = std::list<CUnit*>;
// A list of units holding a reference to each (the AI memory's lists).
using HeldUnits = std::list<HeldRef<CUnit>>;

// What an order does (AIOrder::kind). A unit's queued order (CUnitServer::action2) is UnitOrderKind::None when nothing is
// queued; a queued UnitOrderKind::Idle stops the current order and the unit stands.
enum class UnitOrderKind : int {
    Idle = 0,
    Move = 1,
    Turn = 2,
    Attack = 3,
    CastAt = 4,    // a spell (or a quick item's) at a point
    CastOn = 5,    // a spell (or a quick item's) on a target
    Use = 6,       // a use or a skill on a target (source: UseSource)
    Animation = 7,
    Emote = 8,
    None = 9,
    UseItem = 10,  // a QuickItemCategory::Consumable quick item (source: the quick slot)
    Stop = 11,
};

// AIOrder::source of a UnitOrderKind::Use order (CUnitServer::UseSkill).
enum class UseSource : std::uint32_t {
    Use = 0,
    Steal = 1,
    Tame = 2,
    SearchBody = 0x18A95,
};

// An attack order's source and OrderAttack's location: every body part.
inline constexpr int kAttackAllParts = 6;

// An order as the controller's order checks take it (built on the stack).
struct AIOrder {
    // UnitOrderKind::None: the object released and cleared, the flag cleared.
    void Clear();
    // Clear, then an attack on `object` (referenced) at every body part (kAttackAllParts).
    void MakeAttack(CMapObject* object);
    // Clear, then a cast of `spell` on `object` (referenced).
    void MakeCast(int spell, CMapObject* object);
    // Clear, then a use of `object` with `what`.
    void MakeUse(CMapObject* object, UseSource what);
    // `source` read as a use order's skill (kind Use).
    UseSource Skill() const { return static_cast<UseSource>(source); }
    // Where the order aims: the object's AimPoint, or `point` without an object. Returns `out`.
    Vector3* AimPoint(Vector3* out) const;

    UnitOrderKind kind = UnitOrderKind::None;
    std::uint32_t source = 0;       // the body part of an attack (kAttackAllParts), the spell of a cast, the UseSource of a use
    Ref<CMapObject> target;
    Vector3 point{};                // target point
    // `point` is a use spot of the target that FindUseSpot picked: the unit walks to it and acts from there
    // (no sight or range checks against the target; reach 0.2).
    bool pointIsUseSpot = false;
};

// One remembered look direction of the current move.
struct AILook {
    float angle;
    float time;                     // turning time
    float wait;                     // wait afterwards (game ticks)
    bool stopFirst;
};

// The looks from / to a save: the count, then angle, time and wait of each and (from version 0x44) the stop
// flag.
void LoadLooks(std::vector<AILook>* looks, CStream* stream);
void SaveLooks(const std::vector<AILook>* looks, CStream* stream);

// A move the modes prepare on the stack: where to go and what to look at there.
struct AIMoveOrder {
    void Clear();

    Vector3 point{};
    std::vector<AILook> looks;
};

// Mode types, as the brain names them in its "new motivation" message.
enum class AIModeType : int {
    Guard = 0,
    Patrol = 1,
    Sentry = 2,
    Suspection = 3,
    Aggression = 4,
    CorpseWatcher = 5,
    Player = 6,
    Fear = 7,
    Tamed = 8,
    Follow = 10,
};

// Mode priorities (AIMode::Evaluate): the idle modes' claim, an Aggression mode's while it has an enemy, none.
inline constexpr int kPriorityIdle = 50;
inline constexpr int kPriorityFight = 10000;
inline constexpr int kPriorityNone = -1000;

// The x of a position not set yet (AIBrain's positions, FearMode::shelter).
inline constexpr float kUnsetCoordinate = -1313.0f;

// Base of all modes (abstract). AIActionMode is the base of the modes that act. The brain owns its modes
// (AIBrain::modes).
class AIMode {
public:
    explicit AIMode(AIModeType modeType) : type(modeType) {}
    AIMode(const AIMode&) = delete;
    AIMode& operator=(const AIMode&) = delete;
    virtual ~AIMode() = default;

    // The priority the mode claims this tick; here kPriorityIdle.
    virtual int Evaluate(AIBrain* brain);
    // Runs while the mode is in charge. Here: the idle remark (1 in 1001), then LookAround.
    virtual void Execute(AIBrain* brain);
    // Taking charge. Here: drop the action and target, pick a new alertness decay (0.8 + 0.4 * random) and clear
    // the controller's busy flag.
    virtual void Enter(AIBrain* brain);
    virtual void Leave(AIBrain*) {}
    // True for most modes; AddMode replaces a mode of another type when both answer true.
    virtual bool Exclusive() { return true; }
    virtual void OnObjectDeleted(CEngineObject*) {}
    // Taken out of the brain while in charge (AIBrain::RemoveModesOfType): the mode stays in charge until another
    // takes over, with the fields the original's shutdown of a still-held mode resets.
    virtual void Detach() {}
    virtual void Load(CStream*) {}
    virtual void Save(CStream*) {}

    const AIModeType type;
};

// Base of the modes that act: Suspection, Aggression, CorpseWatcher, Fear and Player.
class AIActionMode : public AIMode {
public:
    explicit AIActionMode(AIModeType modeType) : AIMode(modeType) {}
    int Evaluate(AIBrain* brain) override = 0;
    void Execute(AIBrain* brain) override = 0;
    void Enter(AIBrain* brain) override;
    bool Exclusive() override { return false; }
};

// Guard: stay near a point.
class GuardMode : public AIMode {
public:
    GuardMode() : AIMode(AIModeType::Guard) {}   // a mode AIBrain::Load fills
    GuardMode(const Vector3& point, float radius, float wander) : GuardMode() { Set(point, radius, wander); }
    // Sets the point and the two radii (negative: the globals).
    void Set(const Vector3& point, float radius, float wander);
    // When idle: walk to a free place near the point (after 100 failures, near the unit) and look around
    // there.
    void Execute(AIBrain* brain) override;
    void Detach() override { failures = 0; }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 point{};
    float radius = 0.0f;
    float wander = 0.0f;
    int failures = 0;
};

struct PatrolPoint {
    Vector3 position;
    std::vector<AILook> looks;
};

// Patrol: walk a list of points.
class PatrolMode : public AIMode {
public:
    PatrolMode() : AIMode(AIModeType::Patrol) {}
    // kPriorityIdle; without points: FatalError "Unit %i has no patrol points, switching to guard" (which exits
    // unless the Rebirth patch DisableExitOnCrashReport is on), a point at the unit's position, and kPriorityNone.
    int Evaluate(AIBrain* brain) override;
    // When idle: on to the next point and its looks.
    void Execute(AIBrain* brain) override;
    void Detach() override;
    PatrolPoint* LastPoint();       // null without points
    // The point count, per point its position, look count and per look angle, time, wait and (from version
    // 0x44) the stop flag byte; then `current`.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // Appends a point with no looks at ((n + 0.5) / 2, z 0) for n = 2x rounded to nearest (so 0..0.5 above
    // `position`, not the middle of its half-cell).
    void AppendPoint(const Vector3& position);

    std::vector<PatrolPoint> points;
    int current = 0;
};

// Sentry: stand at a point.
class SentryMode : public AIMode {
public:
    SentryMode() : AIMode(AIModeType::Sentry) {}   // a mode AIBrain::Load fills
    explicit SentryMode(const Vector3& at) : SentryMode() { point = at; }
    void Execute(AIBrain* brain) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 point{};
};

// Follow: stay with a leader.
class FollowMode : public AIMode {
public:
    explicit FollowMode(CMapObject* newLeader = nullptr) : AIMode(AIModeType::Follow) { leader.Assign(newLeader); }
    void Execute(AIBrain* brain) override;
    void OnObjectDeleted(CEngineObject* object) override;
    void Detach() override { leader.Reset(); }
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Ref<CMapObject> leader;
};

// Suspection: go and look where something was noticed.
class SuspectionMode : public AIActionMode {
public:
    SuspectionMode() : AIActionMode(AIModeType::Suspection) {}
    // Remembers a non-allied seen object, then takes over the brain's noticed point and level (the level only
    // grows down to the noticed one while the point is the same).
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain* brain) override;
    // Busy; walk to the point; remark 5.
    void Enter(AIBrain* brain) override;
    // Clears busy; remark 3.
    void Leave(AIBrain* brain) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // The original leaves these unset, but Evaluate keeps a point that compares the same (a NaN always does)
    // and the lower level, so every new mode starts zeroed: a determinism fix (the harness zeroes the
    // original's alike).
    Vector3 point{};
    int level = 0;
};

// AITargetChoice::part and AIAttackOption::part of the weapon (the whole unit as the part to hit).
inline constexpr int kWeaponPart = -1;

// A target the attention chose or the brain is attacking: the part (kWeaponPart, else the spell's part) and the
// object.
struct AITargetChoice {
    int part = kWeaponPart;
    CUnit* object = nullptr;
};

// Aggression: fight an enemy.
class AggressionMode : public AIActionMode {
public:
    // No enemy, the whole unit as the part to hit; `fixed` (UMRevenge) says the script set the enemy.
    explicit AggressionMode(bool isFixed = false) : AIActionMode(AIModeType::Aggression), fixed(isFixed) {}
    // kPriorityFight while there is an enemy to fight (the script's, or the best one in sight), else kPriorityNone.
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain* brain) override;
    // Busy; drops action and target; remark 6.
    void Enter(AIBrain* brain) override;
    // Forgets the noticed level when the enemy is gone or dead, drops the enemy, clears busy; remark 4.
    void Leave(AIBrain* brain) override;
    void OnObjectDeleted(CEngineObject* object) override;
    void Detach() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Ref<CMapObject> enemy;
    bool fixed;                     // enemy set by the script (no searching)
    AITargetChoice chosen;          // the enemy chosen this tick
};

// CorpseWatcher: reports corpses the brain has seen.
class CorpseWatcherMode : public AIActionMode {
public:
    CorpseWatcherMode() : AIActionMode(AIModeType::CorpseWatcher) {}
    // Takes one seen corpse off the brain's list and notices it (kNoticeCorpseLevel). Never takes charge
    // (kPriorityNone).
    int Evaluate(AIBrain* brain) override;
    void Execute(AIBrain*) override {}
};

// Fear: run from an enemy.
class FearMode : public AIActionMode {
public:
    // Distance, spread 3 and an unset shelter (x = -1313). The other fields start zeroed (a determinism fix: the
    // original leaves shelter y / z and fleePoint x / y, which the saves write; the harness zeroes the original's
    // alike).
    explicit FearMode(bool isWatchful) : AIActionMode(AIModeType::Fear), watchful(isWatchful) {}
    // Remembers a seen object and calms `spread`; 15000 for a ranged unit that can run, 3000 while it sees an
    // enemy, 350 in shelter, else noticedLevel / 6 or kPriorityNone. See docs/notes/unit_ai.md, "FearMode::Evaluate".
    int Evaluate(AIBrain* brain) override;
    // Runs from the nearest enemy (calling allies to it), else stays in shelter, else from the noticed point
    // (forgotten unless `watchful` and above 500): within `distance`, and not walking on command, to `fleePoint`
    // when one is pending, else 5 away plus a random offset of at most `spread` (which first drops to 2.5 x
    // `distance`), when a path is found (action 1); `spread` doubles. Without an action: attack the best
    // target in sight (allies only), else stand and look away from the point.
    void Execute(AIBrain* brain) override;
    void Enter(AIBrain* brain) override;
    void Leave(AIBrain* brain) override;
    // The constructor's values again (the rest of the shelter and the flight point stay).
    void Detach() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // A new Fear mode's distance, which `distance` returns to.
    static constexpr float kDefaultDistance = 10.0f;

    float distance = kDefaultDistance;  // how near danger counts
    float spread = 3.0f;            // random spread of a flight point
    Vector3 shelter{kUnsetCoordinate, 0.0f, 0.0f};  // the last place with a SpellArea occluder
    Vector3 fleePoint{};            // a checked flight point (z 1.0 while pending, else 0)
    // Notices player units within `distance` and keeps noticed levels above 500 (UMFear's argument, the
    // fearful reactions).
    bool watchful;
};

// Player: the party's units.
class PlayerMode : public AIActionMode {
public:
    enum { kStateFollow = 0, kStateCommand = 1, kStateMoveTo = 2 };
    static constexpr int kPriority = 10;

    PlayerMode() : PlayerMode(AIModeType::Player) {}
    int Evaluate(AIBrain*) override { return kPriority; }

    // State 0: follow the leader (unless the party is in danger) and fight (unless the leader carries out a
    // command); state 1: wait for the commanded action to end; state 2: walk to `point` (staying with the party
    // unless it is in danger) and fight on the way.
    void Execute(AIBrain* brain) override;
    void OnObjectDeleted(CEngineObject* object) override;
    // The references released, the point, state and run flag cleared.
    void Detach() override;
    // The target's and the leader's references, the point and the state. Saves of versions 0x46 .. 0x5D hold
    // a group of objects after them (ScriptGroup::Load), which is read and dropped.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // The player's commands (state 1 or 2). Walk to `to` + `offset` (just `to` when the way there is
    // blocked); forgets the leader unless the party is in danger.
    void CommandWalk(AIBrain* brain, const Vector3& to, int inGroup, bool run, const Vector3& offset);
    // The same in state 2 (keep fighting on the way); when the unit would leave the party it only turns
    // towards the point.
    void CommandGoTo(AIBrain* brain, const Vector3& to, int inGroup, bool run, const Vector3& offset);
    void CommandAttack(AIBrain* brain, CMapObject* object, int how, int calledShot);
    void CommandUse(AIBrain* brain, CMapObject* object, UseSource kind, int spot, bool run);
    void CommandCast(AIBrain* brain, int spell, CMapObject* object);
    void CommandCastAt(AIBrain* brain, int spell, const Vector3& point);

    // Follow `leader` (counted): none when it is the unit itself. When the chain of leaders from `leader`
    // (each unit's first Player mode) comes back to this unit, this unit's own Player mode stops following
    // first. A walk in progress (action 1) is dropped (state 0).
    void SetLeader(AIBrain* brain, CUnit* leader);

    // Only for units with fightAtWill 1: the mode's target when it can be reached from here (attacked at
    // once); else the best target (no allies) among the enemies near `point` (within [Logic] SwarmRadius in
    // state 2, anywhere otherwise, the point then zeroed) that are no farther than 1.1 x their own sight or
    // hearing, plus what the party is fighting. When there is one, the party's other followers that are not
    // attacking or casting stop (unless the party is in danger), the unit attacks it unless it
    // already does, and the mode's target is dropped.
    void UpdateCombat(AIBrain* brain);
    // Adds to `targets` (once each) the units that party members within [Logic] PlayerCallForHelp are
    // attacking (action 3), unless they belong to the same player.
    void AddPartyTargets(AIBrain* brain, UnitList* targets);
    // The unit, the chain of leaders above it (Player mode's leader, or the followed unit), and repeatedly
    // every unit of its player whose leader is listed (the player's units in hash order, up to the first
    // null one).
    UnitList CollectParty(AIBrain* brain);
    // False when a unit of the player whose (first) Player mode follows this unit is farther than [Logic]
    // FollowMaxDist x (2 while `moving`, else 1.4) + 1 (3D).
    bool StaysWithParty(AIBrain* brain, bool moving);
    // False when `from` and `to` are within 0.1 (2D), else a path search with the unit moved to `from` for it
    // (range twice the distance).
    bool PathClear(AIBrain* brain, const Vector3& from, const Vector3& to);

    Ref<CMapObject> target;
    Ref<CUnit> leader;
    Vector3 point{};                // (z: 1.0 while walking there in state 2)
    int state = kStateFollow;
    bool runs = false;              // the commanded walk runs (and so do the casts after it)

protected:
    explicit PlayerMode(AIModeType modeType) : AIActionMode(modeType) {}
};

// True when the unit's player's party is in danger (CPlayerServer::danger InDanger: a party unit fights or is an
// enemy's target). Its units then fight on their own: they keep their leader on a walk command, do not wait for
// the followers and do not stop the others to fight.
bool PartyInDanger(const CUnit* unit);

// Tamed (type 8): the Player mode a taming installs; it always takes charge.
class TamedMode : public PlayerMode {
public:
    static constexpr int kPriority = 100000000;

    TamedMode() : PlayerMode(AIModeType::Tamed) {}
    int Evaluate(AIBrain*) override { return kPriority; }
};

// A count read from `stream`, then that many counted references appended to `list`, each loaded in `context`.
void CountedListLoad(std::list<CUnit*>* list, CStream* stream, CWorld* context);
void CountedListLoad(HeldUnits* list, CStream* stream, CWorld* context);

// The AI's perception log: the AI stream.
inline constexpr console::Stream& kAiLog = console::g_aiLogOut;

// A call the perception memory heard.
struct AICallRecord {
    Vector3 point;
    int delay;                          // updates until the call is heard (counts down below 0)
    int hostileMask;                    // sides the hearer turns hostile to (0 for a plain call)
    std::string text;
};

struct AIMemory;

struct AIHeardCalls : std::vector<AICallRecord> {
    // Counts every call down; one that runs out is dropped after it is heard: with a hostility mask the
    // unit takes the mask (logged "hear delayed sound"), else, when the memory's noticed level is below
    // kNoticeCallLevel, the call's point becomes the noticed point (logged when the level rose by more than 100
    // or the point lies more than sqrt(10) away).
    void Update(CUnit* unit, AIMemory* memory);
    // The calls without a mask that would be heard no sooner than `delay` are dropped, then {point, delay, 0,
    // text} is appended.
    void Hear(const Vector3& point, int delay, std::string_view text);
    // Appends {point not set, delay, mask, text}: when it runs out (Update) the unit takes the hostility mask.
    void AddClaim(std::uint32_t mask, int delay, std::string_view text);
    void RemoveClaims();
};

// An object the memory reported (the object is a counted reference): a target the brain gave up on, which the
// choice and the enemy searches leave out until the report runs out.
struct AIReport {
    HeldRef<CMapObject> object;
    int ticksLeft;                  // updates the report lasts (dropped below 0)
};

struct AIReports : std::vector<AIReport> {
    // How long a report lasts: a target whose cast the controller stopped, a target the choice rejected.
    static constexpr int kCastStoppedTicks = 0xF;
    static constexpr int kRejectedTicks = 0x14;

    // Keeps the larger `ticks` of a listed object, else appends it (with a reference).
    void Report(CMapObject* object, int ticks);
    bool Contains(CEngineObject* object) const;
};

// What an AI attack option does (AIAttackOption::kind). The spells' kinds by spell type: the arrows and the two
// curse magics attack, Fireball, Invoke Lightning and Column of Acid hit an area, the walls and the acid fog hit an
// area for a time, Stun, Weaken and Feeblemind curse an enemy, the protections, Antimagic and Strengthen protect,
// the sights, Detect Life, Invisibility, Silence, Lichdom, Regeneration and Speed enhance, Healing heals; the rest
// the AI does not use.
enum class AIOptionKind : int {
    Weapon = 0,       // the unit's weapon or natural attack
    SpellAttack = 1,
    AreaAttack = 2,
    LastingArea = 3,  // effect: the spell's duration
    Curse = 4,        // on enemies (the weapons list); effect: the spell type
    Protect = 5,      // on allies; effect: the spell type
    Enhance = 6,      // on allies, not when only allies are chosen; effect: the spell type
    Heal = 7,
    Unused = 8,
};

// One way the unit can attack or help: a weapon (part kWeaponPart) or a spell.
struct AIAttackOption {
    // How much the option counts: a weapon its largest damage, a spell attack that * 1.3, a curse, protection or
    // enhancement 1, healing 10, other kinds 0.
    double Weight() const;
    // The option from / to a save. Before version 0x5A only the part, a dropped dword and the damage were
    // saved: the kind becomes part >= 0, the range 1, the rest 0.
    void Load(CStream* stream);
    void Save(CStream* stream) const;

    AIOptionKind kind;
    int part;                       // kWeaponPart, else the spell
    float range;
    int maxCount;                   // cap on the group-size bonus
    float radius;                   // the area of an area attack, the healing of AIOptionKind::Heal
    int effect;                     // see AIOptionKind

    // The spell type `effect` holds for a curse, protection or enhancement.
    SpellType EffectSpell() const { return static_cast<SpellType>(effect); }
    DamageWords damage;             // measured against the target's protection
};

// The count, the array sized to it, then each option.
void LoadAttackOptions(std::vector<AIAttackOption>* options, CStream* stream);

// The brain's attention: the unit's attack options (AIBrain::Choose picks a target with them).
struct AIAttention {
    const AIAttackOption* FindOption(int part) const;

    // Lists the unit's options: its weapon (unless its natural weapon is 0x10, "spells only") and attack
    // spells (kinds 1..4) into `weapons`, helping spells (5..7) into `spells`; a unit whose id is a text key
    // gets only the weapon. Then `healWeight` is the largest Weight of the `weapons` options.
    void Build(CUnit* unit);
    void InitOption(AIAttackOption* option, AIOptionKind kind, int part, float range, int maxCount, float radius, int effect);
    DamageWords* AddWeaponOption(AIOptionKind kind, int part, float range, int maxCount, float radius, int effect);
    DamageWords* AddSpellOption(AIOptionKind kind, int part, float range, int maxCount, float radius, int effect);

    std::vector<AIAttackOption> weapons;
    std::vector<AIAttackOption> spells;
    float healWeight;               // the largest Weight of the weapons (Build); kind 7 scores with it
};

// The noticed levels and their decays (added to the level every update; AIMemory::Notice): something seen or a
// target lost from sight, a call heard, a corpse seen.
inline constexpr int kNoticeSeenLevel = 1000;
inline constexpr int kNoticeSeenDecay = -2;
inline constexpr int kNoticeCallLevel = 900;
inline constexpr int kNoticeCallDecay = -3;
inline constexpr int kNoticeCorpseLevel = 800;
inline constexpr int kNoticeCorpseDecay = -3;

// The brain's perception memory: what the unit sees, has seen and heard.
struct AIMemory {
    void Reset();
    // Ages the reports (one tick off each; an entry below 0 is dropped), updates the heard calls and the AI
    // events' view, then adds the noticed decay to the noticed level. Both callees may notice a point first (a
    // call, a corpse, a unit lost from sight).
    void Update(CUnitServer* owner);
    // True when the unit can still keep track of `other`: it has the unit's side or player, or it is in sight.
    bool CanTrack(CUnit* self, CUnit* other);
    // Takes over a noticed point of a higher `level`: true when the level rose by more than 100 or the point
    // lies more than sqrt(10) away (with the point's own z, not a difference); false when the level is not
    // higher.
    bool Notice(const Vector3& point, int level, int decay);
    void Forget(CEngineObject* object);
    void HearCall(const Vector3& point, int delay, std::string_view text);
    void AddClaim(std::uint32_t mask, int delay, std::string_view text);
    // The visible units hostile to the unit (its IsHostileTo), alive and not reported, in visible-list order
    // (no references taken).
    UnitList FindEnemies(CUnit* self);
    // The same with the visible unit's own IsHostileTo(self); one at or beyond `radius` of `point` (3D; all
    // of them for a radius <= 0) only when it has an aggression mode (not a fixed one without an enemy) whose
    // enemy is none, not a unit, or a unit of a side the unit's side is allied with, and its brain sees an
    // enemy.
    UnitList FindEnemiesNear(CUnit* self, const Vector3& point, float radius);
    CUnit* NearestEnemy(CUnit* self);
    // Forces `object` into sight when it is not visible and attacks the unit or stands at or beyond one of
    // the two sight ranges: a corpse joins the seen corpses (and the corpses when allied), a living unit the
    // visible list's head (both counted); each logged.
    void ForceSee(CUnit* self, CUnit* object);
    void SeeCorpse(CUnit* self, CUnit* corpse);
    // The unit's exposure to the living hostile units nearby into `out` (CUnit::exposure): per kind the
    // smallest distance / range ratio, at most 3 (a zero range counts 3). [0] sight (scaled by the line of
    // sight between the eyes, only within range), [4], [1], [2] three other ranges; [3] stays 3.
    void MeasureExposure(CUnitServer* self, float* out);
    // From / to a save: the four object lists (appended to on load), the noticed point, level and countdown,
    // the reports, the heard calls (point, delay, text, hostileMask) and the noticed decay (-1 until read).
    void Load(CStream* stream);
    void Save(CStream* stream);

    SpiralCache spareCells{0, -1.0f};   // not read
    SpiralCache sightCells{0, -1.0f};   // the cells around the unit it looks for units in
    float noticedX = 0.0f;
    float noticedY = 0.0f;
    int noticedLevel = 0;
    int noticedDecay = 0;            // the decay: added to noticedLevel every update
    int nearbyCountdown = 0;        // updates until the nearby units are listed again
    HeldUnits nearby;               // the units in the sight cells around
    HeldUnits visible;              // units in sight
    HeldUnits seenCorpses;          // corpses seen
    HeldUnits corpses;              // allied corpses seen
    AIReports reports;              // targets left out for a while
    AIHeardCalls calls;             // calls heard
    bool seesEnemy = false;         // a living hostile unit is in sight (Think)
};

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
    static constexpr float kRangeCleared = 100000.0f;  // nearRange / farRange without a mode that sets them
    static constexpr float kInitialCallRadius = 10.0f;

    // One AI tick: alarm countdown, decay of alertness, perception update, then every mode's Evaluate; the
    // highest (the current mode gets +100) takes over (Leave / Enter, with a "new motivation" console
    // message), its Execute runs, and the brain carries out its action.
    void Think();

    AIMode* FindMode(AIModeType type);
    PatrolMode* FindPatrolMode();
    PlayerMode* FindPlayerMode();   // the first mode of type Player (not Tamed)
    CMapObject* LeaderOrFollowed();
    // The enemy of the first Aggression mode while that mode is in charge, else null.
    CMapObject* AggressionEnemy();
    bool IsNearPath(const Vector3& point, float radius);
    // True when `object` is a unit of a side the brain's unit is allied with.
    bool IsAlliedUnit(CEngineObject* object);
    // Sets the owner; on a change both remembered positions become unset (kUnsetCoordinate, 0, 0).
    void SetOwner(CUnitServer* newOwner);
    // An object is being deleted: tell every mode, drop it as target or seen object and from the perception
    // memory.
    void OnObjectDeleted(CEngineObject* object);

    // Adds `mode` at the end of the list after removing the first mode of the same type, or the first exclusive
    // one when `mode` is exclusive (no mode is in charge when it is the one removed).
    void AddMode(std::unique_ptr<AIMode> mode);
    // A removed mode in charge stays in charge, detached (removedCurrent).
    void RemoveModesOfType(AIModeType type);
    // Empties the mode list, drops the mode in charge, resets the two ranges and the decay.
    void ClearModes();
    // ClearModes, then no action, memory reset, action point and looks cleared, target and seen object
    // released (the controller's death).
    void Reset();
    // Back to a new brain's state (the controller is reused): Reset's work plus owner, alert and alarm state,
    // alarm records, positions.
    void Init();
    // The brain from / to a save: the action, its point, looks and time, target and seen object, the scalars,
    // the modes (count, the index of the one in charge, then each mode's type and its own data), the
    // perception memory, the alarm records, and (by file version) the attack options. Load empties the mode
    // list first, creates each mode by type (stopping at an unknown type, 9 or above 10) and maps a call
    // radius equal to [Logic] CallForHelpRadiusDefault to CallForHelpRadius.
    void Load(CStream* stream);
    void Save(CStream* stream);
    // The standard mode sets of the script's Guard / Sentry commands: ClearModes, then the guard (radius,
    // wander -1: the default) or sentry mode, Suspection, Aggression, CorpseWatcher and Fear(0); the target
    // is released and the action dropped.
    void StandardGuard(const Vector3& point, float radius);
    void StandardSentry(const Vector3& point);
    // The modes of behaviour `record` (type 1 guards, 5 the world alarm point, 3 a sentry, 2 patrols, 4 the
    // party's Player mode, plus the reaction's modes), then the alarm, no action or target, the attack
    // options rebuilt. See docs/notes/unit_ai.md, "AIBrain::ApplyBehaviour".
    void ApplyBehaviour(const mapspec::Behaviour* record, int slot);
    // The records copied into `alarmRecords`, then the first applied.
    void SetAlarmRecords(const mapspec::Behaviour* records, int count);
    // The world alarm `slot` was raised: the behaviour record of that slot is applied (the first always, the
    // others only when enabled) and the owner's voice raises remark 7. Nothing for a slot without a record.
    void HearAlarm(int slot);
    // The perception memory emptied, then the behaviour record of alarmSlot applied again.
    void ResetBehaviour();

    // The best {part, object} to attack or help: the current attack scored first, then (without `alliesOnly`)
    // every weapon against every candidate and every spell against the unit and its allies in sight; the best
    // that scores above the current one and passes IsUsable, the order check (with `alliesOnly`, for targets not
    // near the path) and CanReach wins; a rejected one seen once is reported (20). {kWeaponPart, null} without
    // options.
    AITargetChoice Choose(UnitList* candidates, const Vector3& from, bool alliesOnly, bool fromHere);
    // False when the part has no option or an area option (kinds 2, 3) would also hit an ally near the target
    // or the unit itself; true without an object.
    bool IsUsable(const AITargetChoice* choice);
    // False for a spell aimed at no object against a large unit of size class 0; else the order is checked
    // from the unit's place when `fromHere` is set or the target is the unit itself, otherwise from the end
    // of a path search to the target (range ScaledValue of the distance, the target lifted off the sector map
    // meanwhile), or the unit's place without one.
    bool CanReach(const AITargetChoice* choice, bool fromHere);
    // Chooses the best target in sight (Choose from the unit's position, allies only) and takes it up
    // (ApplyAttack).
    void LookAround();
    // The current attack as {part, object}: action 3 {kWeaponPart, target}, action 4 {spell, target}, the object
    // only when it is a unit; otherwise {kWeaponPart, null}.
    AITargetChoice CurrentAttack();
    // Takes up {part, object} unless there is no object or it is the current attack: kWeaponPart AttackTarget,
    // else CastOn.
    void ApplyAttack(const AITargetChoice* choice);

    // Action 2 (move) after arrival: run the queued looks, each turning then waiting.
    void UpdateMove();
    // Action 3 (attack): give up when the target is dead, no longer an enemy or the controller stopped
    // attacking it.
    void UpdateAttack();
    void StopAction();
    // Action 2: walk to `order`'s point (the target released), then run its looks.
    void StartMove(const AIMoveOrder* order);
    void Walk(const Vector3& point, int run);
    void TurnTowards(const Vector3& direction, float time);
    // Action 4 (unless the spell comes from a scroll): cast `spell` at `point`.
    void CastAt(int spell, const Vector3& point, int run);
    // Action 4 (unless the spell comes from a scroll) on `object` (the target, counted): cast `spell` at it.
    void CastOn(int spell, CMapObject* object, int run);
    void Follow(CMapObject* object);
    // Something was noticed: remember `object` as seen, call allies to it (or to the unit itself when null)
    // unless it is an ally, become alerted in alert mode 1, and remember a unit in the perception memory.
    void SetSeen(CMapObject* object);
    void AttackTarget(CMapObject* object);
    // Action 5: use something on `object` (`kind` the use order's UseSource).
    void UseOn(CMapObject* object, UseSource kind, int spot, int run);

    // Action 6 (follow `target`): every few ticks walk to where the leader is or will be
    // (a moving leader is met ahead on its way, or passed on the side); a dead or lost leader ends it.
    void UpdateFollow();
    // Replaces the looks of `order` by one to four random directions (each the first that the AI map sees
    // clear, within 6 tries) around `direction`, each followed by a random wait between a quarter and all of
    // the wander time (40 when negative, else 15 x `wander`). A `far` look (not for busy units nor "unhu*"
    // prototypes) is a single one with a longer wait.
    void PlanLooks(AIMoveOrder* order, const Vector3& direction, float wander, bool far);
    // A random place within `radius` of `point` to stand at: not too steep, no unit of an allied player near
    // it, and a path there not much longer than the way. After a failure the next calls (up to 10, a third of
    // the distance) fail at once.
    bool FindStandPoint(const Vector3& point, float radius, Vector3* out);
    // Every allied unit within callRadius of the owner's unit hears a call for help (delay kCallDelay) at
    // `point`; the message text is formatted only while the console shows filter bit 0.
    void CallAllies(const Vector3& point);
    static constexpr int kCallDelay = 0x1A;

    CUnitServer* owner = nullptr;
    int action = kActionNone;
    AIMoveOrder move;               // the action point, and what to look at after arriving
    int actionTime = kNoTime;       // game time the current look ends (or kNoTime / kLookStarted)
    Ref<CMapObject> target;
    Ref<CMapObject> seen;
    bool alerted = false;
    std::int8_t alarm = 0;          // alarm kind raised when the countdown ends
    std::int8_t alarmSlot = 0;      // the world alarm slot of the applied record (ApplyBehaviour)
    bool alwaysUpdated = false;     // the world keeps the unit in its update map (the record's flag bit 3)
    float callRadius = kInitialCallRadius;  // allies within it hear a call for help
    int alertMode = 0;
    int alarmCountdown = 1;
    // Two distances the idle modes set (35 and 80, plus the radius for Guard). Nothing reads them: they are only
    // saved and loaded.
    float nearRange = kRangeCleared;
    float farRange = kRangeCleared;
    // An alertness decay factor: moved towards 1 every tick and randomised when an idle mode takes charge. Nothing
    // decides by it: it is kept for the save and for the random draw.
    float decay = 1.0f;
    Vector3 previousPosition{};
    Vector3 position{};
    int standCooldown = 0;          // FindStandPoint fails while above 0 (one off per call)
    int followTimer = 0;            // UpdateFollow acts once it has counted past 8
    std::list<std::unique_ptr<AIMode>> modes;
    AIMode* current = nullptr;      // the mode in charge: one of `modes`, or `removedCurrent`
    // The mode in charge after RemoveModesOfType took it out of `modes` (detached): it stays in charge until
    // another one takes over, as the original's counted reference kept it.
    std::unique_ptr<AIMode> removedCurrent;
    std::vector<mapspec::Behaviour> alarmRecords;
    AIAttention attention;
    AIMemory memory;                // perception memory
};

