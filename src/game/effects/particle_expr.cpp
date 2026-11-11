// The data effects' expressions: the compiler (a recursive descent over the grammar of particle_expr.h) and the
// stack machine that runs them. More: docs/notes/effects.md, "Expressions".
#include "game/effects/particle_expr.h"

#include "game/core/random.h"

#include "lib/maths.h"
#include "lib/vecmath.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace effectdata {
namespace {

constexpr int kMaxDepth = 64;   // the run's stack; a deeper expression does not compile

struct FunctionName {
    std::string_view name;
    Function function = Function::Sin;
    int arguments = 0;
};
constexpr FunctionName kFunctions[] = {
    {"sin", Function::Sin, 1},       {"cos", Function::Cos, 1},     {"atan2", Function::Atan2, 2},
    {"sqrt", Function::Sqrt, 1},     {"abs", Function::Abs, 1},     {"floor", Function::Floor, 1},
    {"ceil", Function::Ceil, 1},     {"round", Function::Round, 1}, {"min", Function::Min, 2},
    {"max", Function::Max, 2},       {"clamp", Function::Clamp, 3}, {"lerp", Function::Lerp, 3},
    {"pow", Function::Pow, 2},       {"range", Function::Range, 2}, {"below", Function::Below, 1},
    {"chance", Function::Chance, 1}, {"pick", Function::Pick, 1},   {"exp", Function::Exp, 1},
    {"log", Function::Log, 1},       {"ground", Function::Ground, 2},
    {"platform_height", Function::PlatformHeight, 2},     {"platform_normal_x", Function::PlatformNormalX, 2},
    {"platform_normal_y", Function::PlatformNormalY, 2},  {"platform_normal_z", Function::PlatformNormalZ, 2},
    {"surface", Function::Surface, 2},
    {"ball_x", Function::BallX, 0},  {"ball_y", Function::BallY, 0}, {"ball_z", Function::BallZ, 0},
};

bool IsNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsNameChar(char c) { return IsNameStart(c) || (c >= '0' && c <= '9') || c == '.'; }

class Compiler {
public:
    Compiler(std::string_view text, const Resolver& resolve) : text(text), resolve(resolve) {}

    bool Run(Expr* out) {
        Ternary();
        Skip();
        if (error.empty() && at < text.size()) Fail("unexpected '" + std::string(1, text[at]) + "'");
        if (!error.empty()) return false;
        out->code = std::move(code);
        return true;
    }

    std::string error;

private:
    void Fail(const std::string& what) {
        if (error.empty()) error = "column " + std::to_string(at + 1) + ": " + what;
    }
    void Skip() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
    }
    bool Take(std::string_view token) {
        Skip();
        if (text.substr(at, token.size()) != token) return false;
        at += token.size();
        return true;
    }
    // A one-character operator that is not the start of a two-character one (`<` but not `<=`).
    bool TakeAlone(char c, char notFollowedBy) {
        Skip();
        if (at >= text.size() || text[at] != c) return false;
        if (at + 1 < text.size() && text[at + 1] == notFollowedBy) return false;
        ++at;
        return true;
    }
    void Emit(Op op, std::uint16_t index = 0, float value = 0.0f, Scope scope = Scope::Source, std::uint8_t count = 0) {
        code.push_back({op, scope, count, index, value});
    }
    void Push(int n = 1) {
        depth += n;
        if (depth > kMaxDepth) Fail("too deep");
    }
    void Pop(int n = 1) { depth -= n; }
    std::uint16_t Here() const { return static_cast<std::uint16_t>(code.size()); }
    void Patch(std::size_t jump) { code[jump].index = Here(); }

    void Ternary() {
        Or();
        if (!Take("?")) return;
        const std::size_t toElse = code.size();
        Emit(Op::JumpIfFalse);
        Pop();
        Ternary();
        if (!Take(":")) return Fail("':' expected");
        const std::size_t toEnd = code.size();
        Emit(Op::Jump);
        Pop();   // the two branches leave one value between them
        Patch(toElse);
        Ternary();
        Patch(toEnd);
    }
    void Or() {
        And();
        while (Take("||")) {
            const std::size_t jump = code.size();
            Emit(Op::OrJump);
            Pop();
            And();
            Emit(Op::Truth);
            Patch(jump);
        }
    }
    void And() {
        Compare();
        while (Take("&&")) {
            const std::size_t jump = code.size();
            Emit(Op::AndJump);
            Pop();
            Compare();
            Emit(Op::Truth);
            Patch(jump);
        }
    }
    void Compare() {
        Sum();
        for (;;) {
            Op op;
            if (Take("<=")) op = Op::LessEqual;
            else if (Take(">=")) op = Op::GreaterEqual;
            else if (Take("==")) op = Op::Equal;
            else if (Take("!=")) op = Op::NotEqual;
            else if (TakeAlone('<', '=')) op = Op::Less;
            else if (TakeAlone('>', '=')) op = Op::Greater;
            else return;
            Sum();
            Emit(op);
            Pop();
        }
    }
    void Sum() {
        Product();
        for (;;) {
            Op op;
            if (Take("+")) op = Op::Add;
            else if (Take("-")) op = Op::Subtract;
            else return;
            Product();
            Emit(op);
            Pop();
        }
    }
    void Product() {
        Unary();
        for (;;) {
            Op op;
            if (Take("*")) op = Op::Multiply;
            else if (Take("/")) op = Op::Divide;
            else if (Take("%")) op = Op::Modulo;
            else return;
            Unary();
            Emit(op);
            Pop();
        }
    }
    void Unary() {
        if (Take("-")) {
            Unary();
            // A negative number stays one constant ("-1" is constant).
            if (!code.empty() && code.back().op == Op::Constant && code.size() == constantAt + 1) {
                code.back().value = -code.back().value;
                return;
            }
            Emit(Op::Negate);
            return;
        }
        if (TakeAlone('!', '=')) {
            Unary();
            Emit(Op::Not);
            return;
        }
        Primary();
    }
    void Primary() {
        Skip();
        if (at >= text.size()) return Fail("a value expected");
        const char c = text[at];
        if (c == '(') {
            ++at;
            Ternary();
            if (!Take(")")) Fail("')' expected");
            return;
        }
        if ((c >= '0' && c <= '9') || c == '.') return Number();
        if (IsNameStart(c)) return Name();
        Fail("unexpected '" + std::string(1, c) + "'");
    }
    void Number() {
        float value = 0.0f;
        const char* first = text.data() + at;
        const auto [end, failed] = std::from_chars(first, text.data() + text.size(), value);
        if (failed != std::errc()) return Fail("not a number");
        at += static_cast<std::size_t>(end - first);
        constantAt = code.size();
        Emit(Op::Constant, 0, value);
        Push();
    }
    void Name() {
        const std::size_t start = at;
        while (at < text.size() && IsNameChar(text[at])) ++at;
        const std::string_view name = text.substr(start, at - start);
        if (Take("(")) return Call(name);
        const Resolved meaning = Lookup(name);
        if (Take("[")) {
            if (meaning.kind != Resolved::Kind::Table && meaning.kind != Resolved::Kind::ListAxis &&
                meaning.kind != Resolved::Kind::AnchorAt)
                return Fail("'" + std::string(name) + "' is no table");
            Ternary();
            if (!Take("]")) return Fail("']' expected");
            if (meaning.kind == Resolved::Kind::ListAxis)
                Emit(Op::ListAt, meaning.index, 0.0f, Scope::Source, meaning.axis);
            else if (meaning.kind == Resolved::Kind::AnchorAt)
                Emit(Op::AnchorAt, meaning.index, 0.0f, meaning.scope, meaning.group);
            else
                Emit(Op::Index, meaning.index);
            return;
        }
        switch (meaning.kind) {
        case Resolved::Kind::Value:
            Emit(Op::Load, meaning.index, 0.0f, meaning.scope);
            Push();
            return;
        case Resolved::Kind::Constant:
            constantAt = code.size();
            Emit(Op::Constant, 0, meaning.constant);
            Push();
            return;
        case Resolved::Kind::Table: return Fail("table '" + std::string(name) + "' needs an index: " + std::string(name) + "[i]");
        case Resolved::Kind::Curve: return Fail("curve '" + std::string(name) + "' needs a value: " + std::string(name) + "(t)");
        case Resolved::Kind::ListAxis: return Fail("list '" + std::string(name) + "' needs an index: " + std::string(name) + "[i]");
        case Resolved::Kind::AnchorAt:
            return Fail("'" + std::string(name) + "' is an anchor's: it needs the anchor's number: " + std::string(name) + "[i]");
        case Resolved::Kind::None: return Fail("unknown name '" + std::string(name) + "'");
        }
    }
    Resolved Lookup(std::string_view name) {
        if (name == "pi") return {Resolved::Kind::Constant, Scope::Source, 0, kPi};
        if (name == "deg") return {Resolved::Kind::Constant, Scope::Source, 0, kPi / 180.0f};
        if (name == "true") return {Resolved::Kind::Constant, Scope::Source, 0, 1.0f};
        if (name == "false") return {Resolved::Kind::Constant, Scope::Source, 0, 0.0f};
        return resolve ? resolve(name) : Resolved{};
    }
    void Call(std::string_view name) {
        int arguments = 0;
        if (!Take(")")) {
            for (;;) {
                Ternary();
                ++arguments;
                if (Take(")")) break;
                if (!Take(",")) return Fail("',' or ')' expected");
            }
        }
        for (const FunctionName& f : kFunctions) {
            if (f.name != name) continue;
            if (arguments != f.arguments)
                return Fail(std::string(name) + " takes " + std::to_string(f.arguments) + " values");
            Emit(Op::Call, static_cast<std::uint16_t>(f.function), 0.0f, Scope::Source, static_cast<std::uint8_t>(arguments));
            Pop(arguments - 1);
            return;
        }
        const Resolved meaning = Lookup(name);
        if (meaning.kind != Resolved::Kind::Curve) return Fail("unknown function '" + std::string(name) + "'");
        if (arguments != 1) return Fail("curve '" + std::string(name) + "' takes one value");
        Emit(Op::CurveAt, meaning.index);
    }

    std::string_view text;
    const Resolver& resolve;
    std::size_t at = 0;
    int depth = 0;
    std::size_t constantAt = static_cast<std::size_t>(-1);   // the last Constant's place
    std::vector<Instruction> code;
};

float CallFunction(Function f, const float* a, const Env& env) {
    IsaacRandom* random = env.random;
    switch (f) {
    case Function::Sin: return lib::Sin(a[0]);
    case Function::Cos: return lib::Cos(a[0]);
    case Function::Atan2: return lib::Atan2(a[0], a[1]);
    case Function::Sqrt: return std::sqrt(a[0]);
    case Function::Abs: return std::fabs(a[0]);
    case Function::Floor: return std::floor(a[0]);
    case Function::Ceil: return std::ceil(a[0]);
    case Function::Round: return std::round(a[0]);
    case Function::Min: return a[1] < a[0] ? a[1] : a[0];
    case Function::Max: return a[1] > a[0] ? a[1] : a[0];
    case Function::Clamp: return a[0] < a[1] ? a[1] : (a[0] > a[2] ? a[2] : a[0]);
    case Function::Lerp: return a[0] + (a[1] - a[0]) * a[2];
    case Function::Pow: return lib::Pow(a[0], a[1]);
    case Function::Exp: return lib::Exp(a[0]);
    case Function::Log: return lib::Log(a[0]);
    case Function::Range: return random ? random->Range(a[0], a[1]) : a[0];
    case Function::Below: return random ? random->Below(a[0]) : 0.0f;
    case Function::Chance: {
        const std::uint32_t roll = random ? random->Next() % 100u : 0u;
        return static_cast<float>(roll) < a[0] ? 1.0f : 0.0f;
    }
    case Function::Pick: {
        const std::uint32_t word = random ? random->Next() : 0u;
        if (!(a[0] >= 1.0f)) return 0.0f;
        const float n = a[0] < 4294967295.0f ? a[0] : 4294967295.0f;
        return static_cast<float>(word % static_cast<std::uint32_t>(n));
    }
    case Function::Ground: return env.ground ? env.ground(a[0], a[1]) : 0.0f;
    case Function::BallX: {
        const Vector3 v = random ? RandomInBall(*random) : Vector3{};
        if (env.ball) *env.ball = v;
        return v.x;
    }
    case Function::BallY: return env.ball ? env.ball->y : 0.0f;
    case Function::BallZ: return env.ball ? env.ball->z : 0.0f;
    case Function::PlatformHeight:
    case Function::PlatformNormalX:
    case Function::PlatformNormalY:
    case Function::PlatformNormalZ: {
        const int which = static_cast<int>(f) - static_cast<int>(Function::PlatformHeight);
        if (env.platform) return env.platform(a[0], a[1], which);
        return which == 3 ? 1.0f : 0.0f;
    }
    case Function::Surface:
        if (env.platform) return env.platform(a[0], a[1], 4);
        return env.ground ? env.ground(a[0], a[1]) : 0.0f;
    }
    return 0.0f;
}

// A table's entry at `at` truncated, held to the table's ends (a NaN reads the first).
float TableAt(const std::vector<float>& table, float at) {
    if (table.empty()) return 0.0f;
    if (!(at >= 1.0f)) return table.front();
    const float last = static_cast<float>(table.size() - 1);
    if (at >= last) return table.back();
    return table[static_cast<std::size_t>(at)];
}

// Axis `axis` of point `at` of a list (x y z each), truncated and held to the ends as a table's entry (empty: 0).
float ListAt(const std::vector<float>& points, float at, int axis) {
    const std::size_t count = points.size() / 3;
    if (count == 0) return 0.0f;
    std::size_t point = 0;
    if (at >= static_cast<float>(count - 1)) point = count - 1;
    else if (at >= 1.0f) point = static_cast<std::size_t>(at);
    return points[point * 3 + static_cast<std::size_t>(axis)];
}

} // namespace

Vector3 RandomInCube(IsaacRandom& random) {
    const float x = random.Range(-1.0f, 1.0f);
    const float y = random.Range(-1.0f, 1.0f);
    return {x, y, random.Range(-1.0f, 1.0f)};
}

Vector3 RandomInBall(IsaacRandom& random, float* lengthSquared, int tries, const Vector3* shift) {
    Vector3 v;
    float length2 = 0.0f;
    if (tries <= 0) {
        for (;;) {
            v = RandomInCube(random);
            if (shift) v = v + *shift;
            length2 = LengthSquared(v);
            if (length2 <= 1.0f && length2 > 0.0f) break;   // random: never NaN
        }
    } else {
        for (;;) {
            v = RandomInCube(random);
            if (shift) v = v + *shift;
            length2 = LengthSquared(v);
            if (length2 < 1.0f) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
    }
    if (lengthSquared) *lengthSquared = length2;
    return v;
}

float Curve::At(float t) const {
    if (x.empty()) return 0.0f;
    if (!(t > x.front())) return y.front();
    for (std::size_t i = 1; i < x.size(); ++i) {
        if (t > x[i]) continue;
        const float span = x[i] - x[i - 1];
        if (!(span > 0.0f)) return y[i];
        return y[i - 1] + (y[i] - y[i - 1]) * ((t - x[i - 1]) / span);
    }
    return y.back();
}

float Expr::Run(const Env& env, float fallback) const {
    if (code.empty()) return fallback;
    std::array<float, kMaxDepth> stack;
    int top = -1;
    for (std::size_t pc = 0; pc < code.size();) {
        const Instruction& i = code[pc++];
        switch (i.op) {
        case Op::Constant: stack[++top] = i.value; break;
        case Op::LoadMeasured:
            if (env.measure) env.measure(env.measureContext);
            [[fallthrough]];
        case Op::Load: {
            const float* scope = env.scopes[static_cast<int>(i.scope)];
            stack[++top] = scope ? scope[i.index] : 0.0f;
            break;
        }
        case Op::Negate: stack[top] = -stack[top]; break;
        case Op::Not: stack[top] = stack[top] == 0.0f ? 1.0f : 0.0f; break;
        case Op::Truth: stack[top] = stack[top] != 0.0f && stack[top] == stack[top] ? 1.0f : 0.0f; break;
        case Op::Add: --top; stack[top] = stack[top] + stack[top + 1]; break;
        case Op::Subtract: --top; stack[top] = stack[top] - stack[top + 1]; break;
        case Op::Multiply: --top; stack[top] = stack[top] * stack[top + 1]; break;
        case Op::Divide: --top; stack[top] = stack[top] / stack[top + 1]; break;
        case Op::Modulo: --top; stack[top] = std::fmod(stack[top], stack[top + 1]); break;
        case Op::Less: --top; stack[top] = stack[top] < stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::LessEqual: --top; stack[top] = stack[top] <= stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::Greater: --top; stack[top] = stack[top] > stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::GreaterEqual: --top; stack[top] = stack[top] >= stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::Equal: --top; stack[top] = stack[top] == stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::NotEqual: --top; stack[top] = stack[top] != stack[top + 1] ? 1.0f : 0.0f; break;
        case Op::Jump: pc = i.index; break;
        case Op::JumpIfFalse: {
            const float c = stack[top--];
            if (!(c != 0.0f && c == c)) pc = i.index;
            break;
        }
        case Op::AndJump:
            if (stack[top] == 0.0f || stack[top] != stack[top]) {
                stack[top] = 0.0f;
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::OrJump:
            if (stack[top] != 0.0f && stack[top] == stack[top]) {
                stack[top] = 1.0f;
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::Call:
            top -= i.count - 1;
            stack[top] = CallFunction(static_cast<Function>(i.index), &stack[top], env);
            break;
        case Op::Index:
            stack[top] = env.tables && i.index < env.tables->size() ? TableAt((*env.tables)[i.index], stack[top]) : 0.0f;
            break;
        case Op::CurveAt:
            stack[top] = env.curves && i.index < env.curves->size() ? (*env.curves)[i.index].At(stack[top]) : 0.0f;
            break;
        case Op::ListAt:
            stack[top] = env.lists && i.index < env.lists->size() ? ListAt((*env.lists)[i.index], stack[top], i.count) : 0.0f;
            break;
        case Op::AnchorAt:
            stack[top] = env.anchorAt ? env.anchorAt(env.anchorContext, i.count, i.scope, i.index, stack[top]) : 0.0f;
            break;
        }
    }
    return top >= 0 ? stack[top] : fallback;
}

Expr Compile(std::string_view text, const Resolver& resolve, std::string* error) {
    Expr expr;
    Compiler compiler(text, resolve);
    if (!compiler.Run(&expr)) {
        if (error) *error = compiler.error;
        return {};
    }
    if (expr.code.empty()) {
        if (error) *error = "empty expression";
        return {};
    }
    expr.text = std::string(text);
    return expr;
}

Expr ConstantExpr(float value) {
    Expr expr;
    expr.code.push_back({Op::Constant, Scope::Source, 0, 0, value});
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    expr.text = text;
    return expr;
}

} // namespace effectdata
