#pragma once

// A data effect's record (docs/notes/effects.md, "Data effects"): Res\effects\<name>.effect.toml through the mod
// layers, the layers' files merged per field before it is read, and the effect's own state machine (inputs, vars,
// stages, reactions, anchors) that steps once per tick. Nothing here needs the game: the particle side is
// particle_data.cpp; the unit checks run this part alone (tests/unit/particle_record.cpp).

#include "game/effects/particle_expr.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <memory>
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
// `above` merged over `below` field by field: a table merges key by key; an array of tables merges its elements by
// their `name` (or `event`) with the first element of that key not yet merged, and appends the others; any other value
// replaces the one below.
void MergeOver(RawNode& below, const RawNode& above);

// ---- The record ----------------------------------------------------------------------------------------------------

// The source's values its expressions read by name (Scope::Source), in this order; then one live count per anchor
// group (`anchors.<name>`).
enum class SourceValue : std::uint16_t {
    Size, Height, Growth, Alpha, Param, BaseSize,
    Age,            // ticks since the effect started
    StageAge,       // ticks in the current stage
    Stage,          // its index (`stage.<name>` are the constants)
    CarrierRadius,  // the attached object's radius and its centre's height over the source (0.1 and 0 when free)
    CarrierZ,
    Attached,
    Distance,       // the source's movement this tick
    MotionX, MotionY, MotionZ,
    OriginX, OriginY, OriginZ,
    Particles,      // live particles
    Index,          // the emission's count this tick (while emitting)
    Total,          // particles emitted so far
    WindX, WindY, WindStrength,
    ExtentX, ExtentY, ExtentZ,
    TurnCos, TurnSin,
    Emitting,       // the source's emission flag (the game's StopEmitting and Stop, an effect's `once` clear it)
    Values
};
inline constexpr int kSourceValues = static_cast<int>(SourceValue::Values);

// An anchor's own values (Scope::Anchor, `a.<name>`).
enum class AnchorValue : std::uint16_t { Index, GroupSize, Age, Ticks, X, Y, Z, Values };
inline constexpr int kAnchorValues = static_cast<int>(AnchorValue::Values);

// A particle's own values (Scope::Particle, `p.<name>`); the values before this tick's update.
enum class ParticleValue : std::uint16_t {
    Age, Life, T, X, Y, Z, VX, VY, VZ, Size, Alpha, Frame, Anchor, Index, Values
};
inline constexpr int kParticleValues = static_cast<int>(ParticleValue::Values);
// The vars a particle kind may declare.
inline constexpr int kParticleVars = 6;

// The kinds of input an effect declares: what game code (and later Lua) drives it with.
enum class InputType : std::uint8_t {
    Float,
    Vector,     // three values, read as `<name>.x`, `.y`, `.z`
    Bool,       // 0 or 1
    Event,      // fired: its value is the times it fired before this tick, for one tick
    Enum,       // a value's index; `<name>.<value>` are the constants
};

struct Input {
    std::string name;
    InputType type = InputType::Float;
    Vector3 start;                      // a float's, bool's or enum's value in x
    std::vector<std::string> values;    // an enum's
    int slot = 0;                       // its first value in the input array
};

// A named value with its start and its change per tick (an empty `tick` keeps it).
struct Var {
    std::string name;
    Expr start;
    Expr tick;
};

// What a stage's entry or a reaction does.
struct Action {
    enum class Kind : std::uint8_t {
        Emit,       // `count` particles of `particleKind` (-1: the regular choice), at each live anchor of `anchor` (-1: none)
        Set,        // input `target` to `value`
        Stage,      // stage `target` (-2: the effect ends)
        Effect,     // starts the effect `effect` at the source (a sub-emitter)
        End,        // the effect stops emitting and ends with its particles
        Stop,       // the source stops as the game's Stop stops it (no emission, not kept alive, its carrier let go);
                    // the effect goes on, and the game may start it again
    };
    Kind kind = Kind::Emit;
    Expr count;
    Expr value;
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
        Event,
    };
    On on = On::Start;
    int slot = -1;      // the event input's
    std::string event;  // its name as written (the merge key)
    Expr when;          // the reaction runs only while it holds (empty: always)
    std::vector<Action> actions;
};

// A group of moving points the source owns (rings, orbs, bolt points): made at the effect's start, each with its own
// vars and position relative to the source's origin.
struct Anchor {
    std::string name;
    Expr count;
    Expr ticks;         // each one's life (empty: lasts)
    std::vector<Var> vars;
    Expr x, y, z;       // or, cylindrical: x = cos(angle) * radius, y = sin(angle) * radius
    Expr radius, angle;
    bool cylindrical = false;
};

enum class SpawnShape : std::uint8_t { Point, Box, Ball, Shell, Disc, Ring };
enum class Space : std::uint8_t {
    World,      // a particle moves on its own once emitted
    Source,     // its offset rides the source's origin
    Anchor,     // its offset rides its anchor
};
enum class FrameMode : std::uint8_t { Loop, Hold, Die };
enum class AnchorPick : std::uint8_t { Cycle, Random };

// One kind of particle the effect emits.
struct ParticleKind {
    std::string name;
    Expr when;              // the regular emission takes the first kind for which it holds (empty: always)
    Expr lifetime;
    std::vector<Var> vars;  // at most kParticleVars
    int anchor = -1;        // emitted at one of these anchors
    AnchorPick pick = AnchorPick::Cycle;
    Space space = Space::World;
    SpawnShape shape = SpawnShape::Point;
    Expr radius;
    Expr boxMin[3], boxMax[3];   // the box's (empty: the source's spread)
    Expr offset[3];
    Expr onGround;          // given: the spawn's z is the ground's height plus it
    Expr velocity[3];
    Expr speed;             // outward from the spawn's centre
    Expr drag, gravity[3], wind;
    bool moves = false;     // a velocity, speed, gravity or wind given (else a world-space particle stays put)
    Expr sizeStart, sizeTick, growth;
    std::uint32_t rgb = 0xFFFFFF;
    Expr rgbTick[3];
    Expr alphaStart, alphaTick;
    Expr frameStart, frameDelay, frameTick;
    FrameMode frameMode = FrameMode::Loop;
    Expr kill;
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
    bool readsCarrier = false;  // an expression reads carrier_radius or carrier_z (the carrier's bounds are measured)
    std::string fallback;

    // [server]: read by the server.
    int oneShot = 0;            // the world object lives this many ticks (0: until removed)
    int scriptLifetime = -1;    // CreateParticleSource's lifetime in ticks (-1: forever)

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
    std::vector<Var> vars;
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

// A directed emission an action asked for: a particle of `kind` at anchor `anchor` (-1: none).
struct Queued {
    int kind = -1;
    int anchor = -1;
};

// One source's effect state: what the game drives (inputs, events) and what the data steps (vars, stage, anchors).
class State {
public:
    explicit State(std::shared_ptr<const Record> record);

    // The inputs (game code and later Lua; particle_data.h): false when the effect has no input of that name and type.
    bool SetValue(std::string_view input, float value);
    bool SetVector(std::string_view input, float x, float y, float z);
    bool SetEnum(std::string_view input, std::string_view value);
    bool Fire(std::string_view event);
    // The source stopped emitting by the game's hand: the "stop" reactions run at the next step.
    void Stopped() { stopPending = true; }

    // One tick: the game has written `source` (SourceValue) first. Afterwards rate, chance, emitting, queue, burst,
    // effects and ended say what the source does this tick.
    void Step(IsaacRandom& random);

    // The Env of the source's scopes (an anchor's and a particle's are added by the caller).
    Env SourceEnv(IsaacRandom* random) const;
    // The Env for anchor `index`.
    Env AnchorEnv(int index, IsaacRandom* random) const;

    std::shared_ptr<const Record> record;
    std::vector<float> source;      // kSourceValues, then the anchors' live counts
    std::vector<float> inputs;
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
    bool sawEmitting = true;        // the source's Emitting flag after the last step (particle_data.cpp)

    // This tick's emission.
    float rate = 0.0f;
    float chance = 100.0f;
    bool emitting = true;
    int burst = 0;                  // regular tries added this tick
    std::vector<Queued> queue;
    std::vector<std::string> effects;

private:
    void Start(IsaacRandom& random);
    void Enter(int index, IsaacRandom& random, int depth = 0);
    void Run(const std::vector<Action>& actions, IsaacRandom& random, int depth = 0);
    void MakeAnchors(IsaacRandom& random);
    void StepAnchors(IsaacRandom& random);
    bool Holds(const Reaction& reaction, IsaacRandom& random) const;

    std::vector<int> pendingEvents;     // per input slot: fired since the last step
    bool stopPending = false;
};

} // namespace effectdata
