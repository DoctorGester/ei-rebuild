#pragma once

// The data effects' expressions (docs/notes/effects.md, "Data effects"): a small fixed grammar, compiled once when an
// effect loads into a stack program over floats and run every tick. Numbers, names, + - * / %, comparisons, ! && ||
// (both short), a ? b : c, calls of the functions below or of a curve, a table's entry `name[i]` and an anchor's field by
// its number `group.field[i]`. Every operation is a float operation in the order written, operands left to right, so the
// random draws come in that order too.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class IsaacRandom;
struct Vector3;

namespace effectdata {

// Where a name's value lives when an expression runs: one array per scope (particle_record.h names their entries).
enum class Scope : std::uint8_t {
    Source,         // the source's own values (SourceValue) and its anchors' live counts
    Input,          // the effect's inputs
    SourceVar,      // the effect's [vars]
    Anchor,         // the anchor an expression runs for (AnchorValue)
    AnchorVar,      // that anchor's vars
    Particle,       // the particle an expression runs for (ParticleValue)
    ParticleVar,    // that particle's vars
};
inline constexpr int kScopes = 7;

// A piecewise linear curve over sorted x (the value at the ends held beyond them).
struct Curve {
    std::vector<float> x;
    std::vector<float> y;
    float At(float t) const;
};

// The arrays and tables one run of an expression reads; a scope the context lacks is null (the compiler refuses its
// names there).
struct Env {
    const float* scopes[kScopes] = {};
    const std::vector<std::vector<float>>* tables = nullptr;
    const std::vector<Curve>* curves = nullptr;
    const std::vector<std::vector<float>>* lists = nullptr;     // the list inputs' points, x y z each
    IsaacRandom* random = nullptr;
    // Called before a LoadMeasured: fills the values measured at their first read (null: they stand as they are).
    void (*measure)(void* context) = nullptr;
    void* measureContext = nullptr;
    // The ground's height at (x, y) for `ground(x, y)` (null: 0).
    float (*ground)(float x, float y) = nullptr;
    // The top platform's plane at (x, y) for `platform_height` and `platform_normal_x/y/z` (`which` 0: its height, 1-3:
    // its normal; null: height 0 and normal (0, 0, 1), as off every platform), and `surface` (`which` 4: the ground's
    // height or the platforms' above it, the higher; null: the ground's).
    float (*platform)(float x, float y, int which) = nullptr;
    // A field of an anchor group's anchor by its number (`<group>.<field>[i]`): `scope` Anchor (its own values) or
    // AnchorVar (its vars) (null: 0).
    float (*anchorAt)(const void* context, int group, Scope scope, int field, float number) = nullptr;
    const void* anchorContext = nullptr;
    // The point the last `ball_x()` drew, which `ball_y()` and `ball_z()` read (null: no point kept, they answer 0).
    Vector3* ball = nullptr;
};

// What a name means where an expression is compiled.
struct Resolved {
    enum class Kind : std::uint8_t {
        None, Value, Constant, Table, Curve,
        ListAxis,   // one axis (`axis`) of a list input's points, read as a table
        AnchorAt,   // a field (`index`, of `scope` Anchor or AnchorVar) of anchor group `group`'s anchor by number
    };
    Kind kind = Kind::None;
    Scope scope = Scope::Source;
    std::uint16_t index = 0;    // the value's place in its scope, or the table's, curve's or list's
    float constant = 0.0f;
    std::uint8_t axis = 0;      // a list axis's: 0, 1, 2 for x, y, z
    std::uint8_t group = 0;     // an AnchorAt's anchor group
};
// The names of the context an expression is compiled in: a dotted name (`p.size`, `stage.glow`) to its meaning.
using Resolver = std::function<Resolved(std::string_view name)>;

// One instruction of a compiled expression.
enum class Op : std::uint8_t {
    Constant, Load,
    LoadMeasured,   // a Load of a value measured at its first read (Env::measure runs first)
    Negate, Not, Add, Subtract, Multiply, Divide, Modulo,
    Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual,
    Truth,          // the top as 0 or 1
    Jump,           // to `target`
    JumpIfFalse,    // pops; jumps when it was 0 (or NaN)
    AndJump,        // the top 0: it stays and the run jumps; else it is popped
    OrJump,         // the top not 0: it becomes 1 and the run jumps; else it is popped
    Call,           // a Function with `count` arguments
    Index,          // a table's entry by the top
    CurveAt,        // a curve's value at the top
    ListAt,         // a list input's point by the top, its axis `count`
    AnchorAt,       // a field (`index` of `scope`) of anchor group `count`'s anchor by the top
};

// The functions an expression can call (`range` and `below` draw the effect's generator once, `chance` and `pick`
// once per call).
enum class Function : std::uint8_t {
    Sin, Cos, Atan2, Sqrt, Abs, Floor, Ceil, Round, Min, Max, Clamp, Lerp, Pow,
    Exp, Log,   // e^x and the natural logarithm
    Range,      // range(lo, hi): IsaacRandom::Range
    Below,      // below(k): IsaacRandom::Below
    Chance,     // chance(percent): 1 when Next() % 100 is below it
    Pick,       // pick(n): Next() % n, 0 for n below 1
    Ground,     // ground(x, y): the ground's height there (Env::ground)
    // platform_height(x, y), platform_normal_x / _y / _z(x, y): the plane of the top platform there (Env::platform)
    PlatformHeight, PlatformNormalX, PlatformNormalY, PlatformNormalZ,
    // surface(x, y): what stands there: the ground's height or a platform's above it, the higher (Env::platform 4)
    Surface,
    // ball_x(): a point drawn in the unit ball (RandomInBall: ten tries of the cube, the last kept), its x;
    // ball_y(), ball_z(): the same point's y and z (no draw)
    BallX, BallY, BallZ,
};

struct Instruction {
    Op op = Op::Constant;
    Scope scope = Scope::Source;
    std::uint8_t count = 0;     // a call's arguments, a list's axis
    std::uint16_t index = 0;    // a load's place, a function, a table, a curve or a jump's target
    float value = 0.0f;         // a constant's
};

// A compiled expression. An empty one (no text) is "not given": Run answers `fallback`.
class Expr {
public:
    bool Empty() const { return code.empty(); }
    // Whether it is one number (a constant folded by the compiler or written as one).
    bool IsConstant() const { return code.size() == 1 && code[0].op == Op::Constant; }
    float Run(const Env& env, float fallback = 0.0f) const;

    std::string text;
    std::vector<Instruction> code;
};

// A random point of the cube [-1, 1)^3, drawn x, y, z.
Vector3 RandomInCube(IsaacRandom& random);
// A random point in the unit ball: `tries` draws of the cube, the first inside kept, else the last; `tries` 0: drawn
// until inside, its surface included, and off its centre (0 < length squared <= 1). `shift` (when given) is added to
// each draw before it is tested. `lengthSquared` (when given) gets its squared length.
Vector3 RandomInBall(IsaacRandom& random, float* lengthSquared = nullptr, int tries = 10,
                     const Vector3* shift = nullptr);

// `text` compiled against `resolve`; an empty Expr and `error` set (the column and what is wrong) when it does not
// compile.
Expr Compile(std::string_view text, const Resolver& resolve, std::string* error);
// The number `value` as an expression.
Expr ConstantExpr(float value);

} // namespace effectdata
