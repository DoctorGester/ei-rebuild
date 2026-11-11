#pragma once

// The data effects' expressions (docs/notes/effects.md, "Data effects"): a small fixed grammar, compiled once when an
// effect loads into a stack program over floats and run every tick. Numbers, names, + - * / %, comparisons, ! && ||
// (both short), a ? b : c, calls of the functions below or of a curve, and a table's entry `name[i]`. Every operation
// is a float operation in the order written, operands left to right, so the random draws come in that order too.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class IsaacRandom;

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
    IsaacRandom* random = nullptr;
};

// What a name means where an expression is compiled.
struct Resolved {
    enum class Kind : std::uint8_t { None, Value, Constant, Table, Curve };
    Kind kind = Kind::None;
    Scope scope = Scope::Source;
    std::uint16_t index = 0;    // the value's place in its scope, or the table's or curve's
    float constant = 0.0f;
};
// The names of the context an expression is compiled in: a dotted name (`p.size`, `stage.glow`) to its meaning.
using Resolver = std::function<Resolved(std::string_view name)>;

// One instruction of a compiled expression.
enum class Op : std::uint8_t {
    Constant, Load, Negate, Not, Add, Subtract, Multiply, Divide, Modulo,
    Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual,
    Truth,          // the top as 0 or 1
    Jump,           // to `target`
    JumpIfFalse,    // pops; jumps when it was 0 (or NaN)
    AndJump,        // the top 0: it stays and the run jumps; else it is popped
    OrJump,         // the top not 0: it becomes 1 and the run jumps; else it is popped
    Call,           // a Function with `count` arguments
    Index,          // a table's entry by the top
    CurveAt,        // a curve's value at the top
};

// The functions an expression can call (`range` and `below` draw the effect's generator once, `chance` and `pick`
// once per call).
enum class Function : std::uint8_t {
    Sin, Cos, Atan2, Sqrt, Abs, Floor, Ceil, Round, Min, Max, Clamp, Lerp, Pow,
    Range,      // range(lo, hi): IsaacRandom::Range
    Below,      // below(k): IsaacRandom::Below
    Chance,     // chance(percent): 1 when Next() % 100 is below it
    Pick,       // pick(n): Next() % n, 0 for n below 1
};

struct Instruction {
    Op op = Op::Constant;
    Scope scope = Scope::Source;
    std::uint8_t count = 0;     // a call's arguments
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
    // Whether any instruction loads from `scope`.
    bool Reads(Scope scope) const;

    std::string text;
    std::vector<Instruction> code;
};

// `text` compiled against `resolve`; an empty Expr and `error` set (the column and what is wrong) when it does not
// compile.
Expr Compile(std::string_view text, const Resolver& resolve, std::string* error);
// The number `value` as an expression.
Expr ConstantExpr(float value);

} // namespace effectdata
