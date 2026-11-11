#pragma once

// A data effect's record (docs/notes/effects.md, "Data effects"): Res\effects\<name>.effect.toml through the mod
// layers, the layers' files merged per field before it is read, and the effect's own state machine (inputs, vars,
// stages, reactions, anchors) that steps once per tick. Nothing here needs the game: the particle side is
// particle_data.cpp; the unit checks run this part alone (tests/unit/particle_record.cpp).

#include "engine/particles/particle_expr.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toml {
class Value;
}

namespace effectdata {

// ---- The layers' files merged ---------------------------------------------------------------------------------------

// A TOML document copied into a tree of our own, so that the layers' files merge before the record is read.
struct RawNode {
    enum class Kind : std::uint8_t { Missing, Table, Array, String, Integer, Float, Bool };
    Kind kind = Kind::Missing;
    std::string text;
    double number = 0.0;
    bool flag = false;
    std::vector<std::pair<std::string, RawNode>> keys;   // a table's, in the file's order
    std::vector<RawNode> items;                          // an array's
    std::string file;                                    // where it was read, for messages
    int line = 0;

    bool Exists() const { return kind != Kind::Missing; }
    bool IsNumber() const { return kind == Kind::Integer || kind == Kind::Float; }
    // A table's value of `key` (a missing node when there is none).
    const RawNode& operator[](std::string_view key) const;
    // "<file>:<line>" for messages.
    std::string Where() const;
};

// The document `root` as a tree; `file` names it in messages.
RawNode Copy(const toml::Value& root, const std::string& file);
// `above` merged over `below` field by field: a table merges key by key; an array of tables with a `name` (or `event`)
// merges each element with the first element below of that key not yet merged and appends the others; any other value
// (an action list such as `do`, whose tables have neither, an array of numbers) replaces the one below. A key's or an
// element's table with `remove = true` takes the one below out; a new one with `before = "<name>"` goes before that
// one instead of after the last.
void MergeOver(RawNode& below, const RawNode& above);

// The effect `tree` (the layers of the effect `name` merged) built on the effect its `like` names: that effect's tree
// (`layers` answers an effect's layers merged, false when no layer has it), resolved in turn, with `tree` merged over it
// (MergeOver) and `like` taken out; `tree` keeps its file for messages. True when there is no `like`. False, the problem
// in `errors` ("<file>:<line>: ..."), when the base is missing or the chain comes back to an effect already in it.
bool ResolveLike(RawNode& tree, std::string_view name,
                 const std::function<bool(std::string_view effect, RawNode* layers)>& layers,
                 std::vector<std::string>* errors);

// ---- The record ----------------------------------------------------------------------------------------------------

// The source's values its expressions read by name (Scope::Source), in this order; then one live count per anchor
// group (`anchors.<name>`).
enum class SourceValue : std::uint16_t {
    Size, Height, Growth, Alpha, Param, BaseSize,
    Age,            // ticks since the effect started
    StageAge,       // ticks in the current stage
    Stage,          // its index (`stage.<name>` are the constants)
    CarrierRadius,  // the attached object's radius and its centre's height over the source (0.1 and 0 when free),
    CarrierZ,       // measured at their first read in a tick (LoadMeasured: measuring recomputes the carrier's bounds)
    Attached,
    Distance,       // the source's movement this tick
    MotionX, MotionY, MotionZ,
    OriginX, OriginY, OriginZ,
    Particles,      // live particles
    Index,          // the emission's count this tick (while emitting)
    Total,          // particles emitted so far
    WindX, WindY, WindZ, WindStrength,
    ExtentX, ExtentY, ExtentZ,
    TurnCos, TurnSin,
    Emitting,       // the source's emission flag (the game's StopEmitting and Stop, an effect's `once` clear it)
    SpreadMinX, SpreadMinY, SpreadMinZ,     // the spread box (ScaleSize scales it)
    SpreadMaxX, SpreadMaxY, SpreadMaxZ,
    SideX, SideY, SideZ,    // the motion's side and up axes this tick (zero while the source barely moves)
    UpX, UpY, UpZ,
    // The carrier as a unit, written at the tick's start and after an action lets it go (only for an effect that
    // reads one of them): 1 when it is a unit, runs, is dead; its own x axis; its bounds' radius as it stands (not
    // measured again); its feet (the "ll3" / "rl3" parts, else its body, else its position raised by its bounds'
    // top; the source's origin without a unit carrier).
    CarrierUnit, CarrierRunning, CarrierDead,
    CarrierAxisX, CarrierAxisY, CarrierAxisZ,
    CarrierBoundsRadius,
    CarrierLeftFootX, CarrierLeftFootY, CarrierLeftFootZ,
    CarrierRightFootX, CarrierRightFootY, CarrierRightFootZ,
    CarrierAxisYX, CarrierAxisYY, CarrierAxisYZ,    // its own y axis (0 when free)
    CarrierUnitRadius,  // a unit carrier's own radius (Unit::Radius, by its size class; 0 for any other carrier)
    SunR, SunG, SunB,   // the scene's sun colour (0..1 each; 0 without a sun)
    Values
};
inline constexpr int kSourceValues = static_cast<int>(SourceValue::Values);

// An anchor's own values (Scope::Anchor, `a.<name>`).
enum class AnchorValue : std::uint16_t {
    Index,          // its number in its group
    GroupSize,      // the anchors its group made so far
    Age, Ticks,
    PositionX, PositionY, PositionZ,    // `a.position`, relative to the source's origin
    SpawnX, SpawnY, SpawnZ,     // `a.spawn`: its spawn's point, drawn when it is made
    Values
};
inline constexpr int kAnchorValues = static_cast<int>(AnchorValue::Values);

// A particle's own values (Scope::Particle, `p.<name>`); the values before this tick's update.
enum class ParticleValue : std::uint16_t {
    Age, Life, T,
    PositionX, PositionY, PositionZ,    // `p.position`
    VelocityX, VelocityY, VelocityZ,    // `p.velocity`
    Size, Alpha, Frame, Anchor, Index,
    SpawnX, SpawnY, SpawnZ,     // `p.spawn`: its spawn's point, at the emission (0 in the update)
    ColorR, ColorG, ColorB,     // `p.color` (0..255)
    Values
};
inline constexpr int kParticleValues = static_cast<int>(ParticleValue::Values);
// The floats a particle kind's vars hold between them (a vector var takes its lanes).
inline constexpr int kParticleVars = 8;

// The kinds of input an effect declares: what game code (and later Lua) drives it with.
enum class InputType : std::uint8_t {
    Float,
    Vector,     // three values, read as one vector `<name>` (`<name>.x` its x)
    Bool,       // 0 or 1
    Event,      // fired: its value is the times it fired before this tick, for one tick
    Enum,       // a value's index; `<name>.<value>` are the constants
    List,       // points: `<name>.count`, and `<name>[i]` a point (held to the ends)
};

struct Input {
    std::string name;
    InputType type = InputType::Float;
    Vector3 start;                      // a float's, bool's or enum's value in x
    std::vector<std::string> values;    // an enum's
    std::vector<Vector3> points;        // a list's default
    int slot = 0;                      // its first value in the input array (a list's: its count)
    int list = -1;                      // a list's place in State::lists

    // The value a float, bool or enum input holds when set to `value` (a bool's 0 / 1, an enum's index floored); none
    // for another type or an index outside an enum's values.
    std::optional<float> Holds(float value) const;
    // An enum's index of its value `wanted`; -1 when it has none.
    int ValueIndex(std::string_view wanted) const;
};

// A named value with its start and its change per tick (an empty `tick` keeps it; a tick alone also starts it); a vector
// takes its lanes.
struct Var {
    std::string name;
    int slot = 0;               // its first float in its vars' array
    int width = 1;              // its lanes (the widest of its start and tick)
    Expr start;
    Expr tick;
    bool atCreate = false;      // the effect's: its start runs when the source is made, before the first tick
};

// What a stage's entry or a reaction does.
struct Action {
    enum class Kind : std::uint8_t {
        Emit,       // `count` particles of `particleKind` (-1: the regular choice), at each live anchor of `anchor` (-1: none)
        Set,        // input `target` to `value`
        SetVar,     // the effect's var `target` to `value`
        SetSource,  // the source's own value `target` (a SourceValue: size, height, growth, alpha, param) to `value`
        Stage,      // stage `target` (-2: the effect ends)
        Effect,     // starts the effect `effect` at the source (a sub-emitter)
        End,        // the effect stops emitting and ends with its particles
        Stop,       // the source stops as the game's Stop stops it (no emission, not kept alive, its carrier let go);
                    // the effect goes on, and the game may start it again
        StopEmitting,   // the source's emission flag cleared, nothing else (the game may set it again)
        LetGo,      // the source lets go of its carrier (stays where it is; its flags unchanged)
        AddAnchor,  // `count` anchors of group `anchor` made now
        Shake,      // the camera shakes from the source's origin: `value` its strength, `time` its length in seconds,
                    // `decay` its fall per step (Camera::Shake)
        Scorch,     // a scorch mark on the ground under the source's origin: `value` its radius, `length` its radius
                    // along its turn (none: `value`), `alpha` its strength (held to 1), `angle` its turn (none: the
                    // client's generator draws it)
    };
    Kind kind = Kind::Emit;
    Expr count;
    Expr value;
    int width = 1;      // a set's: the floats its target takes
    Expr time, decay;   // a shake's
    Expr alpha, length, angle;   // a scorch's
    int target = -1;
    int particleKind = -1;
    int anchor = -1;
    std::string effect;
};

inline constexpr int kEndStage = -2;

struct Stage {
    std::string name;
    Expr ticks;         // its length, from its entry (empty: until something else moves on)
    int next = -1;      // after `ticks` (-1: stays; kEndStage: the effect ends)
    Expr until;         // moves to `untilNext` once true
    int untilNext = -1;
    std::vector<std::pair<int, int>> onEvent;   // an event input's slot and the stage it moves to
    Expr rate, chance, emitting;                // over [emit]'s while the stage lasts
    std::vector<Action> enter;
};

struct Reaction {
    enum class On : std::uint8_t {
        Start,
        Stop,
        Tick,   // every tick
        Emit,   // after each particle the emission makes (kept or dropped); its actions take effect at once
        BeforeEmit,     // before each particle the emission makes (after its try's chance roll), the same way
        Spawn,  // the world's effect object made the source and caught it up (EffectParticle::Spawn), once
        Event,
    };
    On on = On::Start;
    int slot = -1;      // the event input's
    std::string event;  // its name as written (the merge key)
    Expr when;          // the reaction runs only while it holds (empty: always)
    std::vector<Action> actions;
};

// A group of moving points the source owns (rings, orbs, bolt points): made at the effect's start (`count`) or by an
// `add_anchor` action, each with its own vars and position relative to the source's origin.
struct Anchor {
    std::string name;
    Expr count;
    Expr spawn;         // its point (`spawn = "ball()"`), drawn first when one is made (a.spawn; empty: 0)
    Expr ticks;         // each one's life (empty: lasts)
    std::vector<Var> vars;
    int varFloats = 0;  // the floats its vars hold
    Expr position;      // its start, relative to the source's origin (empty: a.spawn)
    Expr positionTick;  // its place each tick (empty: it stays)
    bool atCreate = false;  // its `count` anchors are made when the source is made (State::Create), ticked from the
                            // first tick on
};
enum class Space : std::uint8_t {
    World,      // a particle moves on its own once emitted
    Source,     // its offset rides the source's origin
    Anchor,     // its offset rides its anchor
};
enum class FrameMode : std::uint8_t { Loop, Hold, Die, Free };
enum class AnchorPick : std::uint8_t { Cycle, Random, First };

// One kind of particle the effect emits.
struct ParticleKind {
    std::string name;
    Expr when;              // the regular emission takes the first kind for which it holds (empty: always)
    Expr lifetime;
    std::vector<Var> vars;  // at most kParticleVars floats between them
    int anchor = -1;        // emitted at one of these anchors
    AnchorPick pick = AnchorPick::Cycle;
    Expr anchorIndex;       // given: the anchor is the group's live one of this number (wrapped), taken after the vars
    Space space = Space::World;
    Expr spawn;             // its point (`spawn = "ball() * size"`), drawn before its vars (p.spawn; empty: 0)
    // The place in its space (world space: the world's; source and anchor space: the offset from the source's origin
    // or from its anchor's place): the start (empty: the centre plus p.spawn) and each tick (empty: moved by the
    // velocity), from this tick's values before any changes.
    Expr position, positionTick;
    Expr velocity;          // its start (empty: 0)
    Expr velocityTick;      // given: the velocity each tick, after the move (empty: it keeps its velocity)
    Expr sizeStart, sizeTick;
    std::uint32_t rgb = 0xFFFFFF;
    Expr color;             // given: the colour each tick (0..255 a lane)
    Expr alphaStart, alphaTick;
    Expr frameStart, frameDelay, frameTick;
    FrameMode frameMode = FrameMode::Loop;
    Expr kill;
    Expr drop;              // holds at the end of the emission: the particle is not kept (its draws were made)
    // Each update, in order: a source value (SourceValue), an effect var or a var of its anchor set.
    struct Write {
        enum class To : std::uint8_t { Source, Var, AnchorVar };
        To to = To::Source;
        int index = 0;
        int width = 1;
        Expr value;
    };
    std::vector<Write> writes;
    Expr jump;              // holds after an update: the particle jumps (its previous place, size and colour made its
                            // new ones, so it is not drawn moving from the old)
    Expr jumpSize;          // given: the previous size a jump gives instead of its size
    std::vector<Action> onDeath;    // at its death: particles emitted and effects started at its place, values set
};

// Whether the source emits only while it is attached to an object (particle_data.cpp makes it ParticleSourceFlag).
enum class Attach : std::uint8_t { Free, AttachedOnly };

struct Record {
    std::string name;       // the file's stem, as written
    std::int32_t id = 0;    // IdOf(name)
    std::string file;       // the top file, for messages

    std::string texture;
    int frames = 16;
    int columns = 4;
    bool additive = false;
    Attach attach = Attach::Free;
    int part = -1;          // an AttachPoint (the source then sits on that part); -1: on top of the bounds
    bool ticksWhilePaused = false, idlesOffScreen = false;
    bool keepAlive = false;     // a free source lasts without particles until it ends (else only while it has some)
    bool readsCarrier = false;  // an expression reads carrier_radius or carrier_z
    bool readsUnit = false;     // an expression reads one of the carrier's unit values (SourceValue::CarrierUnit...)
    bool beforeEmit = false;    // it has "before_emit" reactions
    bool spawns = false;        // it has "spawn" reactions

    // [server]: read by the server.
    int oneShot = 0;            // the world object lives this many ticks (0: until removed)
    int scriptLifetime = -1;    // CreateParticleSource's lifetime in ticks (-1: forever)
    bool wall = false;          // the world object is an EffectWall: it carries a direction, the source's extent

    // [source]: the source's starting values.
    float size = 1.0f, height = 0.0f, growth = 0.0f, alpha = 1.0f, param = 1.0f;
    Vector3 spreadMin, spreadMax, extent;

    // [emit]
    Expr rate, chance, emitting;
    float spacing = 1000.0f;
    int maxParticles = 500;
    bool once = false;          // the source's emission flag cleared at its first particle of the regular emission

    std::vector<Input> inputs;
    int inputValues = 0;
    int lists = 0;              // its list inputs
    std::vector<Var> vars;
    int varFloats = 0;          // the floats its vars hold
    std::vector<Stage> stages;
    std::vector<Reaction> reactions;
    std::vector<Anchor> anchors;
    std::vector<ParticleKind> particles;
    std::vector<std::string> tableNames, curveNames;
    std::vector<std::vector<float>> tables;
    std::vector<Curve> curves;

    // The input named `name`, or null.
    const Input* FindInput(std::string_view name) const;
};

// The id a data effect travels and is saved under: 0x40000000 | the FNV-1a 32 of its lower-case name in the low 30
// bits, apart from the original types' 0x2000 range.
std::int32_t IdOf(std::string_view name);
inline constexpr std::int32_t kDataIdRange = 0x40000000;
inline bool IsDataId(std::int32_t id) { return (id & kDataIdRange) != 0; }

// `root` (the layers merged) read as the effect `name`, whose id is `id` (0: IdOf(name); an original type's for the
// original effects on data). Every problem goes to `errors` ("<file>:<line>: ..."); null when one makes the effect
// unusable (an expression that does not compile, an unknown name), so it shows nothing.
std::shared_ptr<const Record> Read(const RawNode& root, std::string_view name, std::vector<std::string>* errors,
                                   std::int32_t id = 0);

// ---- The state machine ---------------------------------------------------------------------------------------------

struct AnchorState {
    int group = 0;
    bool live = true;
    float values[kAnchorValues] = {};
    std::vector<float> vars;
};

// A directed emission an action asked for: a particle of `kind` at anchor `anchor` (-1: none); `placed`: its spawn's
// centre is `place` (a dying particle's).
struct Queued {
    int kind = -1;
    int anchor = -1;
    bool placed = false;
    Vector3 place;
};

// A camera shake an action asked for (particle_data.cpp shakes the camera from the source's origin).
struct ShakeRequest {
    float strength = 0.0f;
    float time = 0.0f;
    float decay = 0.0f;
};

// A scorch mark an action asked for (particle_data.cpp lays it under the source's origin).
struct ScorchRequest {
    float radius = 0.0f;
    float length = 0.0f;        // the radius along its turn
    float alpha = 0.0f;
    std::optional<float> angle; // none: the client's generator draws it
};

// An effect an action started: at the source's origin, or at `place` (a dying particle's).
struct Started {
    std::string name;
    bool placed = false;
    Vector3 place;
};

// One source's effect state: what the game drives (inputs, events) and what the data steps (vars, stage, anchors).
class State {
public:
    explicit State(std::shared_ptr<const Record> record);

    // The inputs (game code and later Lua; particle_data.h): false when the effect has no input of that name and type.
    bool SetValue(std::string_view input, float value);
    bool SetVector(std::string_view input, float x, float y, float z);
    bool SetEnum(std::string_view input, std::string_view value);
    bool SetList(std::string_view input, const std::vector<Vector3>& points);
    bool Fire(std::string_view event);
    // The source stopped emitting by the game's hand: the "stop" reactions run at the next step.
    void Stopped() { stopPending = true; }

    // The vars and the anchor groups that start when the source is made (`at_create`), from the source values written
    // first.
    void Create(IsaacRandom& random);
    // The "spawn" reactions: the world's effect object made the source (particle_data.cpp then applies their actions).
    void Spawned(IsaacRandom& random);
    // The "emit" reactions, after a particle of the emission (particle_data.cpp then applies their actions).
    void Emitted(IsaacRandom& random);
    // The "before_emit" reactions, before a particle of the emission (the same way).
    void BeforeEmission(IsaacRandom& random);
    // A particle of `kind` died at `place`: its `on_death` actions, their values computed in `env` (the particle's).
    void Died(const ParticleKind& kind, const Env& env, const Vector3& place);

    // One tick: the game has written `source` (SourceValue) first. Afterwards rate, chance, emitting, queue, burst,
    // effects and ended say what the source does this tick.
    void Step(IsaacRandom& random);

    // The carrier's values (SourceValue::CarrierRadius, CarrierZ): `measure` writes them into `source` at their first
    // read in a tick (particle_data.cpp; null: they stand, as in the unit checks). `measured` says they are this tick's.
    void (*measure)(void* context) = nullptr;
    void* measureContext = nullptr;
    bool measured = false;
    // The ground's height at a point (`ground(point)`, `on_ground`; null: 0, as in the unit checks).
    float (*ground)(float x, float y) = nullptr;
    // The top platform's plane at a point (`platform_height`, `platform_normal`, `surface`; Env::platform).
    float (*platform)(float x, float y, int which) = nullptr;

    // The Env of the source's scopes (an anchor's and a particle's are added by the caller).
    Env SourceEnv(IsaacRandom* random) const;
    // The Env for anchor `index`.
    Env AnchorEnv(int index, IsaacRandom* random) const;

    std::shared_ptr<const Record> record;
    std::vector<float> source;      // kSourceValues, then the anchors' live counts
    std::vector<float> inputs;
    std::vector<std::vector<float>> lists;  // the list inputs' points, x y z each
    std::vector<float> vars;
    std::vector<AnchorState> anchors;   // never removed (a particle reads its anchor's last values)
    int stage = -1;
    int stageAge = 0;               // steps since the current stage began
    int stageTicks = -1;            // the current stage's length (-1: none)
    int age = 0;
    int total = 0;                  // particles emitted
    bool started = false;
    bool ended = false;
    bool stopRequested = false;     // a `stop` action ran this tick (particle_data.cpp stops the source)
    bool stopEmitting = false;      // a `stop_emitting` action ran this tick (particle_data.cpp clears the flag)
    bool letGo = false;             // a `let_go` action ran this tick (particle_data.cpp detaches the source)
    std::vector<int> sourceWrites;  // the source values `set` this tick (SourceValue; particle_data.cpp writes them)
    bool sawEmitting = true;        // the source's Emitting flag after the last step (particle_data.cpp)

    // This tick's emission.
    float rate = 0.0f;
    float chance = 100.0f;
    bool emitting = true;
    int burst = 0;                  // regular tries added this tick
    std::vector<Queued> queue;
    std::vector<Started> effects;
    std::vector<ShakeRequest> shakes;
    std::vector<ScorchRequest> scorches;

private:
    // Env::anchorAt: a field of group `group`'s anchor numbered `number` (as made, held to the ends), 0 when it has none.
    static float AnchorAt(const void* context, int group, Scope scope, int field, float number);
    void Start(IsaacRandom& random);
    void Enter(int index, IsaacRandom& random, int depth = 0);
    void Run(const std::vector<Action>& actions, IsaacRandom& random, int depth = 0);
    void React(Reaction::On on, IsaacRandom& random);
    void MakeAnchors(IsaacRandom& random, bool atCreate);
    void AddAnchors(int group, int count, IsaacRandom& random);
    void StepAnchors(IsaacRandom& random);
    bool Holds(const Reaction& reaction, IsaacRandom& random) const;

    std::vector<int> pendingEvents;     // per input slot: fired since the last step
    bool stopPending = false;
};

} // namespace effectdata
