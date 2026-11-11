// The data effects' expressions: the compiler (a recursive descent over the grammar of particle_expr.h, each value's
// width known as it is compiled) and the stack machine that runs them on four lanes. More: docs/notes/effects.md,
// "Expressions".
#include "engine/particles/particle_expr.h"

#include "engine/core/random.h"

#include "lib/maths.h"
#include "lib/vecmath.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace effectdata {
namespace {

constexpr int kMaxDepth = 32;   // the run's stack; a deeper expression does not compile
constexpr int kMostArguments = 3;   // a function's (clamp, lerp)

// The lanes a function's result fills, from its arguments' (`argument`, joined lane by lane).
enum class Answer : std::uint8_t {
    PerLane,    // as its arguments
    Scalar,     // one value
    Point,      // three lanes
};

struct FunctionName {
    std::string_view name;
    Function function = Function::Sin;
    int fewest = 0, most = 0;   // its arguments
    Answer answer = Answer::PerLane;
};
constexpr FunctionName kFunctions[] = {
    {"sin", Function::Sin, 1, 1},          {"cos", Function::Cos, 1, 1},
    {"atan2", Function::Atan2, 2, 2},      {"sqrt", Function::Sqrt, 1, 1},
    {"abs", Function::Abs, 1, 1},          {"floor", Function::Floor, 1, 1},
    {"ceil", Function::Ceil, 1, 1},        {"round", Function::Round, 1, 1},
    {"min", Function::Min, 2, 2},          {"max", Function::Max, 2, 2},
    {"clamp", Function::Clamp, 3, 3},      {"lerp", Function::Lerp, 3, 3},
    {"pow", Function::Pow, 2, 2},          {"exp", Function::Exp, 1, 1},
    {"log", Function::Log, 1, 1},          {"range", Function::Range, 2, 2},
    {"below", Function::Below, 1, 1},      {"chance", Function::Chance, 1, 1, Answer::Scalar},
    {"pick", Function::Pick, 1, 1, Answer::Scalar},
    {"ground", Function::Ground, 1, 1, Answer::Scalar},
    {"platform_height", Function::PlatformHeight, 1, 1, Answer::Scalar},
    {"platform_normal", Function::PlatformNormal, 1, 1, Answer::Point},
    {"surface", Function::Surface, 1, 1, Answer::Scalar},
    {"on_ground", Function::OnGround, 2, 2, Answer::Point},
    {"dot", Function::Dot, 2, 2, Answer::Scalar},
    {"length", Function::Length, 1, 1, Answer::Scalar},
    {"normalize", Function::Normalize, 1, 1},
    {"cross", Function::Cross, 2, 2, Answer::Point},
    {"distance", Function::Distance, 2, 2, Answer::Scalar},
    {"ball", Function::Ball, 0, 0, Answer::Point},
    {"shell", Function::Shell, 0, 0, Answer::Point},
    {"disc", Function::Disc, 0, 0, Answer::Point},
    {"circle", Function::Circle, 1, 1, Answer::Point},
};

bool IsNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsNameChar(char c) { return IsNameStart(c) || (c >= '0' && c <= '9') || c == '.'; }

// The lane of a swizzle letter (x y z w or r g b a), -1 for another.
int SwizzleLane(char c) {
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
}

// The lanes of a swizzle (`xyz`, `rgb`: one to four letters of one set), empty when `text` is none.
std::vector<int> SwizzleLanes(std::string_view text) {
    std::vector<int> lanes;
    if (text.empty() || text.size() > 4) return {};
    const bool colour = text.find_first_of("rgba") != std::string_view::npos;
    for (const char c : text) {
        const int lane = SwizzleLane(c);
        if (lane < 0 || colour != (std::string_view("rgba").find(c) != std::string_view::npos)) return {};
        lanes.push_back(lane);
    }
    return lanes;
}

class Compiler {
public:
    Compiler(std::string_view text, const Resolver& resolve, bool* loosened)
        : text(text), resolve(resolve), loosened(loosened) {}

    bool Run(Expr* out) {
        const int width = Ternary();
        Skip();
        if (error.empty() && at < text.size()) Fail("unexpected '" + std::string(1, text[at]) + "'");
        if (!error.empty()) return false;
        out->code = std::move(code);
        out->width = width;
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
    Instruction& Emit(Op op, std::uint16_t index = 0, int width = 1) {
        Instruction& i = code.emplace_back();
        i.op = op;
        i.index = index;
        i.width = static_cast<std::uint8_t>(width);
        return i;
    }
    void Push(int n = 1) {
        depth += n;
        if (depth > kMaxDepth) Fail("too deep");
    }
    void Pop(int n = 1) { depth -= n; }
    std::uint16_t Here() const { return static_cast<std::uint16_t>(code.size()); }
    void Patch(std::size_t jump) { code[jump].index = Here(); }
    // Two values' lanes combined lane by lane: a scalar goes with any vector; two vectors need the same lanes.
    int Mixed(int a, int b) {
        if (a == b || b == 1) return a;
        if (a == 1) return b;
        Fail("a vector of " + std::to_string(a) + " and one of " + std::to_string(b) + " do not mix");
        return a;
    }

    int Ternary() {
        const int condition = Or();
        if (!Take("?")) return condition;
        const std::size_t toElse = code.size();
        Emit(Op::JumpIfFalse);
        Pop();
        const int a = Ternary();
        if (!Take(":")) {
            Fail("':' expected");
            return a;
        }
        const std::size_t toEnd = code.size();
        Emit(Op::Jump);
        Pop();   // the two branches leave one value between them
        Patch(toElse);
        const int b = Ternary();
        Patch(toEnd);
        return Mixed(a, b);
    }
    int Or() {
        int width = And();
        while (Take("||")) {
            const std::size_t jump = code.size();
            Emit(Op::OrJump);
            Pop();
            And();
            Emit(Op::Truth);
            Patch(jump);
            width = 1;
        }
        return width;
    }
    int And() {
        int width = Compare();
        while (Take("&&")) {
            const std::size_t jump = code.size();
            Emit(Op::AndJump);
            Pop();
            Compare();
            Emit(Op::Truth);
            Patch(jump);
            width = 1;
        }
        return width;
    }
    int Compare() {
        int width = Sum();
        for (;;) {
            Op op;
            if (Take("<=")) op = Op::LessEqual;
            else if (Take(">=")) op = Op::GreaterEqual;
            else if (Take("==")) op = Op::Equal;
            else if (Take("!=")) op = Op::NotEqual;
            else if (TakeAlone('<', '=')) op = Op::Less;
            else if (TakeAlone('>', '=')) op = Op::Greater;
            else return width;
            width = Mixed(width, Sum());
            Emit(op, 0, width);
            Pop();
        }
    }
    int Sum() {
        int width = Product();
        for (;;) {
            Op op;
            if (Take("+")) op = Op::Add;
            else if (Take("-")) op = Op::Subtract;
            else return width;
            width = Mixed(width, Product());
            Emit(op, 0, width);
            Pop();
        }
    }
    int Product() {
        int width = Unary();
        for (;;) {
            Op op;
            if (Take("*")) op = Op::Multiply;
            else if (Take("/")) op = Op::Divide;
            else if (Take("%")) op = Op::Modulo;
            else return width;
            width = Mixed(width, Unary());
            Emit(op, 0, width);
            Pop();
        }
    }
    int Unary() {
        if (Take("-")) {
            const int width = Unary();
            // A negative number stays one constant ("-1" is constant).
            if (!code.empty() && code.back().op == Op::Constant && code.size() == constantAt + 1) {
                code.back().value = -code.back().value;
                return width;
            }
            Emit(Op::Negate, 0, width);
            return width;
        }
        if (TakeAlone('!', '=')) {
            const int width = Unary();
            Emit(Op::Not, 0, width);
            return width;
        }
        return Postfix(Primary());
    }
    // Swizzles after a value: `(a + b).xy`, `path[i].z`.
    int Postfix(int width) {
        while (at + 1 < text.size() && text[at] == '.' && IsNameStart(text[at + 1])) {
            const std::size_t start = ++at;
            while (at < text.size() && IsNameStart(text[at])) ++at;
            width = Swizzle(text.substr(start, at - start), width);
        }
        return width;
    }
    // The swizzle `letters` of the value on top (of `width` lanes); its lanes.
    int Swizzle(std::string_view letters, int width) {
        const std::vector<int> lanes = SwizzleLanes(letters);
        if (lanes.empty()) {
            Fail("'." + std::string(letters) + "' is no swizzle (x y z w or r g b a)");
            return width;
        }
        std::uint16_t packed = 0;
        bool ascending = true;
        bool loose = false;
        for (std::size_t k = 0; k < lanes.size(); ++k) {
            int lane = lanes[k];
            if (lane >= width && loosened) {
                *loosened = loose = true;
                lane = width - 1;
            } else if (lane >= width) {
                Fail("'." + std::string(letters) + "' reads past a value of " + std::to_string(width) +
                     (width == 1 ? " lane" : " lanes"));
                return width;
            }
            packed = static_cast<std::uint16_t>(packed | lane << (2 * k));
            ascending = ascending && !loose && (k == 0 || lane == lanes[k - 1] + 1);
        }
        const int result = static_cast<int>(lanes.size());
        // A load's consecutive lanes are a narrower load (`origin.x` reads the one float).
        Instruction& last = code.back();
        if (ascending && (last.op == Op::Load || last.op == Op::LoadMeasured) && width > 1) {
            last.index = static_cast<std::uint16_t>(last.index + lanes[0]);
            last.width = static_cast<std::uint8_t>(result);
            return result;
        }
        if (width == 1 && result == 1) return 1;   // a scalar's x is itself
        Emit(Op::Swizzle, packed, result);
        return result;
    }
    int Primary() {
        Skip();
        if (at >= text.size()) {
            Fail("a value expected");
            return 1;
        }
        const char c = text[at];
        if (c == '(') {
            ++at;
            const int width = Ternary();
            if (!Take(")")) Fail("')' expected");
            return width;
        }
        if ((c >= '0' && c <= '9') || c == '.') return Number();
        if (IsNameStart(c)) return Name();
        Fail("unexpected '" + std::string(1, c) + "'");
        return 1;
    }
    int Number() {
        float value = 0.0f;
        const char* first = text.data() + at;
        const auto [end, failed] = std::from_chars(first, text.data() + text.size(), value);
        if (failed != std::errc()) {
            Fail("not a number");
            return 1;
        }
        at += static_cast<std::size_t>(end - first);
        constantAt = code.size();
        Emit(Op::Constant).value = value;
        Push();
        return 1;
    }
    int Name() {
        const std::size_t start = at;
        while (at < text.size() && IsNameChar(text[at])) ++at;
        std::string_view name = text.substr(start, at - start);
        if (Take("(")) return Call(name);
        Resolved meaning = Lookup(name);
        // `origin.x`, `aim.yx.x`: a name the context lacks whose last parts are swizzles reads the first parts' lanes.
        std::string_view swizzles;
        for (std::string_view base = name; meaning.kind == Resolved::Kind::None;) {
            const std::size_t dot = base.rfind('.');
            if (dot == std::string_view::npos || SwizzleLanes(base.substr(dot + 1)).empty()) break;
            base = base.substr(0, dot);
            const Resolved first = Lookup(base);
            if (first.kind == Resolved::Kind::Value) {
                meaning = first;
                swizzles = name.substr(dot + 1);
                name = base;
            }
        }
        if (Take("[")) {
            if (meaning.kind != Resolved::Kind::Table && meaning.kind != Resolved::Kind::List &&
                meaning.kind != Resolved::Kind::AnchorAt) {
                Fail("'" + std::string(name) + "' is no table");
                return 1;
            }
            Ternary();
            if (!Take("]")) {
                Fail("']' expected");
                return 1;
            }
            Instruction* i = nullptr;
            int width = 1;
            switch (meaning.kind) {
            case Resolved::Kind::List:
                width = 3;
                i = &Emit(Op::ListAt, meaning.index, 3);
                break;
            case Resolved::Kind::AnchorAt:
                width = meaning.width;
                i = &Emit(Op::AnchorAt, meaning.index, width);
                i->scope = meaning.scope;
                i->count = meaning.group;
                break;
            default: i = &Emit(Op::Index, meaning.index); break;
            }
            return width;
        }
        switch (meaning.kind) {
        case Resolved::Kind::Value: {
            Instruction& load = Emit(Op::Load, meaning.index, meaning.width);
            load.scope = meaning.scope;
            Push();
            int width = meaning.width;
            while (!swizzles.empty()) {
                const std::size_t dot = std::min(swizzles.find('.'), swizzles.size());
                width = Swizzle(swizzles.substr(0, dot), width);
                swizzles.remove_prefix(std::min(dot + 1, swizzles.size()));
            }
            return width;
        }
        case Resolved::Kind::Constant:
            constantAt = code.size();
            Emit(Op::Constant).value = meaning.constant;
            Push();
            return 1;
        case Resolved::Kind::Table:
            Fail("table '" + std::string(name) + "' needs an index: " + std::string(name) + "[i]");
            return 1;
        case Resolved::Kind::Curve:
            Fail("curve '" + std::string(name) + "' needs a value: " + std::string(name) + "(t)");
            return 1;
        case Resolved::Kind::List:
            Fail("list '" + std::string(name) + "' needs an index: " + std::string(name) + "[i]");
            return 1;
        case Resolved::Kind::AnchorAt:
            Fail("'" + std::string(name) + "' is an anchor's: it needs the anchor's number: " + std::string(name) + "[i]");
            return 1;
        case Resolved::Kind::None: Fail("unknown name '" + std::string(name) + "'"); return 1;
        }
        return 1;
    }
    Resolved Lookup(std::string_view name) {
        if (name == "pi") return {Resolved::Kind::Constant, Scope::Source, 0, kPi};
        if (name == "deg") return {Resolved::Kind::Constant, Scope::Source, 0, kPi / 180.0f};
        if (name == "true") return {Resolved::Kind::Constant, Scope::Source, 0, 1.0f};
        if (name == "false") return {Resolved::Kind::Constant, Scope::Source, 0, 0.0f};
        return resolve ? resolve(name) : Resolved{};
    }
    // vec2(...), vec3(...), vec4(...): its values' lanes in order; one scalar fills them all.
    int Construct(std::string_view name, const std::vector<int>& widths) {
        const int want = name[3] - '0';
        if (widths.size() == 1 && widths[0] == 1) {
            Emit(Op::Swizzle, 0, want);   // x in every lane
            return want;
        }
        int total = 0;
        std::uint16_t packed = 0;
        for (std::size_t k = 0; k < widths.size(); ++k) {
            total += widths[k];
            packed = static_cast<std::uint16_t>(packed | widths[k] << (4 * k));
        }
        if (total != want) {
            Fail(std::string(name) + " takes " + std::to_string(want) + " lanes, not " + std::to_string(total));
            return want;
        }
        Instruction& i = Emit(Op::Construct, packed, want);
        i.count = static_cast<std::uint8_t>(widths.size());
        Pop(static_cast<int>(widths.size()) - 1);
        return want;
    }
    int Call(std::string_view name) {
        std::vector<int> widths;
        if (!Take(")")) {
            for (;;) {
                widths.push_back(Ternary());
                if (Take(")")) break;
                if (!Take(",")) {
                    Fail("',' or ')' expected");
                    return 1;
                }
            }
        }
        const int arguments = static_cast<int>(widths.size());
        if (name == "vec2" || name == "vec3" || name == "vec4") {
            if (arguments < 1 || arguments > 4) {
                Fail(std::string(name) + " takes one to four values");
                return 1;
            }
            return Construct(name, widths);
        }
        for (const FunctionName& f : kFunctions) {
            if (f.name != name) continue;
            if (arguments < f.fewest || arguments > f.most) {
                Fail(std::string(name) + " takes " + std::to_string(f.fewest) +
                     (f.most != f.fewest ? " or " + std::to_string(f.most) : "") + " values");
                return 1;
            }
            int lanes = 1;
            for (const int w : widths) lanes = Mixed(lanes, w);
            const int width = f.answer == Answer::Point ? 3 : f.answer == Answer::Scalar ? 1 : lanes;
            Instruction& i = Emit(Op::Call, static_cast<std::uint16_t>(f.function), width);
            i.count = static_cast<std::uint8_t>(arguments);
            i.argument = static_cast<std::uint8_t>(lanes);
            if (arguments == 0) Push();
            else Pop(arguments - 1);
            return width;
        }
        const Resolved meaning = Lookup(name);
        if (meaning.kind != Resolved::Kind::Curve) {
            Fail("unknown function '" + std::string(name) + "'");
            return 1;
        }
        if (arguments != 1) {
            Fail("curve '" + std::string(name) + "' takes one value");
            return 1;
        }
        Emit(Op::CurveAt, meaning.index);
        return 1;
    }

    std::string_view text;
    const Resolver& resolve;
    bool* loosened = nullptr;   // given: a swizzle past a value's lanes reads its last lane and sets it
    std::size_t at = 0;
    int depth = 0;
    std::size_t constantAt = static_cast<std::size_t>(-1);   // the last Constant's place
    std::vector<Instruction> code;
};

Lanes Splat(float value) { return {{value, value, value, value}}; }

Lanes Point(const Vector3& p) { return {{p.x, p.y, p.z, 0.0f}}; }

// `f` of lanes [0, width), a scalar's repeated in all four.
template <class F>
Lanes PerLane(int width, F f) {
    Lanes r;
    for (int k = 0; k < 4; ++k) r.v[k] = k < width ? f(k) : 0.0f;
    if (width == 1) r = Splat(r.v[0]);
    return r;
}

float Dot(const Lanes& a, const Lanes& b, int lanes) {
    float sum = a.v[0] * b.v[0];
    for (int k = 1; k < lanes; ++k) sum = sum + a.v[k] * b.v[k];
    return sum;
}

float GroundAt(const Env& env, float x, float y) { return env.ground ? env.ground(x, y) : 0.0f; }

float PlatformAt(const Env& env, float x, float y, int which) {
    if (env.platform) return env.platform(x, y, which);
    if (which == 4) return GroundAt(env, x, y);
    return which == 3 ? 1.0f : 0.0f;
}

// A function of the scalar kind (Sin to Below lane by lane, Chance and Pick on lane x) of one lane's arguments `a`.
float ScalarCall(Function f, const float* a, IsaacRandom* random) {
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
    default: return 0.0f;
    }
}
// Whether `f` is of the scalar kind (ScalarCall).
bool IsScalarCall(Function f) { return f <= Function::Pick; }

Lanes CallFunction(const Instruction& i, const Lanes* a, const Env& env) {
    IsaacRandom* random = env.random;
    const int width = i.width;
    // A point argument's x and y.
    const float px = a[0].v[0];
    const float py = a[0].v[1];
    const Function f = static_cast<Function>(i.index);
    if (f == Function::Chance || f == Function::Pick) return Splat(ScalarCall(f, &px, random));
    if (IsScalarCall(f)) {
        return PerLane(width, [&](int k) {
            float lane[kMostArguments] = {};
            for (int n = 0; n < i.count && n < kMostArguments; ++n) lane[n] = a[n].v[k];
            return ScalarCall(f, lane, random);
        });
    }
    switch (f) {
    case Function::Ground: return Splat(GroundAt(env, px, py));
    case Function::PlatformHeight: return Splat(PlatformAt(env, px, py, 0));
    case Function::PlatformNormal:
        return {{PlatformAt(env, px, py, 1), PlatformAt(env, px, py, 2), PlatformAt(env, px, py, 3), 0.0f}};
    case Function::Surface: return Splat(PlatformAt(env, px, py, 4));
    case Function::OnGround: return {{px, py, GroundAt(env, px, py) + a[1].v[0], 0.0f}};
    case Function::Dot: return Splat(Dot(a[0], a[1], i.argument));
    case Function::Length: return Splat(std::sqrt(Dot(a[0], a[0], i.argument)));
    case Function::Distance: {
        Lanes d;
        for (int k = 0; k < 4; ++k) d.v[k] = a[0].v[k] - a[1].v[k];
        return Splat(std::sqrt(Dot(d, d, i.argument)));
    }
    case Function::Normalize: {
        const float length = std::sqrt(Dot(a[0], a[0], width));
        if (!(length > 0.0f)) return a[0];
        const float inverse = 1.0f / length;
        return PerLane(width, [&](int k) { return inverse * a[0].v[k]; });
    }
    case Function::Cross: {
        const float* u = a[0].v;
        const float* w = a[1].v;
        return {{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0], 0.0f}};
    }
    case Function::Ball: return Point(random ? RandomInBall(*random) : Vector3{});
    case Function::Shell: {
        const Vector3 v = random ? RandomInBall(*random) : Vector3{};
        const float length = Length(v);
        return Point(length > 0.0f ? v * (1.0f / length) : Vector3{1.0f, 0.0f, 0.0f});
    }
    case Function::Disc: {
        constexpr float kTwoPi = kPi + kPi;
        const float angle = random ? random->Range(0.0f, kTwoPi) : 0.0f;
        const float reach = std::sqrt(random ? random->Below(1.0f) : 0.0f);
        return {{lib::Cos(angle) * reach, lib::Sin(angle) * reach, 0.0f, 0.0f}};
    }
    case Function::Circle: return {{lib::Cos(px), lib::Sin(px), 0.0f, 0.0f}};
    default: break;   // the scalar kind, above
    }
    return Splat(0.0f);
}

// A table's entry at `at` truncated, held to the table's ends (a NaN reads the first).
float TableAt(const std::vector<float>& table, float at) {
    if (table.empty()) return 0.0f;
    if (!(at >= 1.0f)) return table.front();
    const float last = static_cast<float>(table.size() - 1);
    if (at >= last) return table.back();
    return table[static_cast<std::size_t>(at)];
}

// Point `at` of a list (x y z each), truncated and held to the ends as a table's entry (empty: 0).
Lanes ListAt(const std::vector<float>& points, float at) {
    const std::size_t count = points.size() / 3;
    if (count == 0) return Splat(0.0f);
    std::size_t point = 0;
    if (at >= static_cast<float>(count - 1)) point = count - 1;
    else if (at >= 1.0f) point = static_cast<std::size_t>(at);
    return {{points[point * 3], points[point * 3 + 1], points[point * 3 + 2], 0.0f}};
}

bool True(float x) { return x != 0.0f && x == x; }

// The top two values combined lane by lane into one of `width` lanes (a scalar's computed once).
template <class F>
void Binary(Lanes* stack, int& top, int width, F f) {
    --top;
    Lanes& a = stack[top];
    const Lanes& b = stack[top + 1];
    if (width == 1) a = Splat(f(a.v[0], b.v[0]));
    else
        for (int k = 0; k < 4; ++k) a.v[k] = f(a.v[k], b.v[k]);
}

// Whether an instruction takes and gives scalars alone (its result one lane, its operands too), so that a program of
// such runs on one float a value with the same lane x as on four lanes.
bool ScalarOnly(const Instruction& i) {
    if (i.width != 1 || i.op == Op::Swizzle || i.op == Op::Construct) return false;
    return i.op != Op::Call || i.argument == 1;
}

// Marks the fast forms an expression runs in (Expr::scalar, Expr::scalarParts).
void Classify(Expr& expr) {
    expr.scalar = false;
    expr.scalarParts = 0;
    if (expr.code.empty()) return;
    const Instruction& last = expr.code.back();
    // Its parts one lane each, and no branch that ends past the join (`c ? 1 : vec3(...)` leaves one value).
    bool joined = last.op == Op::Construct && last.count == last.width;
    for (std::size_t k = 0; k + 1 < expr.code.size(); ++k) {
        const Instruction& i = expr.code[k];
        if (!ScalarOnly(i)) return;
        const bool jump = i.op == Op::Jump || i.op == Op::JumpIfFalse || i.op == Op::AndJump || i.op == Op::OrJump;
        if (jump && i.index >= expr.code.size()) joined = false;
    }
    if (ScalarOnly(last)) expr.scalar = true;
    else if (joined) expr.scalarParts = last.count;
}

// The instructions code[0, end) of a program of scalars (Classify) run on one float a value, into `stack`; the top's
// place. Each step is lane x of the four-lane run's (Expr::RunLanes), in the same order, so the draws are the same.
int RunScalars(const std::vector<Instruction>& code, std::size_t end, const Env& env, float* stack) {
    int top = -1;
    for (std::size_t pc = 0; pc < end;) {
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
        case Op::Truth: stack[top] = True(stack[top]) ? 1.0f : 0.0f; break;
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
        case Op::JumpIfFalse:
            if (!True(stack[top--])) pc = i.index;
            break;
        case Op::AndJump:
            if (!True(stack[top])) {
                stack[top] = 0.0f;
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::OrJump:
            if (True(stack[top])) {
                stack[top] = 1.0f;
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::Call: {
            top -= i.count - 1;
            const Function f = static_cast<Function>(i.index);
            if (IsScalarCall(f)) {
                stack[top] = ScalarCall(f, &stack[top], env.random);
                break;
            }
            Lanes arguments[kMostArguments];
            for (int k = 0; k < i.count && k < kMostArguments; ++k) arguments[k] = Splat(stack[top + k]);
            stack[top] = CallFunction(i, arguments, env).v[0];
            break;
        }
        case Op::Index:
            stack[top] = env.tables && i.index < env.tables->size() ? TableAt((*env.tables)[i.index], stack[top]) : 0.0f;
            break;
        case Op::CurveAt:
            stack[top] = env.curves && i.index < env.curves->size() ? (*env.curves)[i.index].At(stack[top]) : 0.0f;
            break;
        case Op::AnchorAt:
            stack[top] = env.anchorAt ? env.anchorAt(env.anchorContext, i.count, i.scope, i.index, stack[top]) : 0.0f;
            break;
        case Op::Swizzle:
        case Op::Construct: break;   // never in a program of scalars (Classify)
        }
    }
    return top;
}

} // namespace

Vector3 RandomInCube(IsaacRandom& random) {
    const float x = random.Range(-1.0f, 1.0f);
    const float y = random.Range(-1.0f, 1.0f);
    return {x, y, random.Range(-1.0f, 1.0f)};
}

Vector3 RandomInBall(IsaacRandom& random) {
    Vector3 v;
    for (int tries = 10;;) {
        v = RandomInCube(random);
        if (LengthSquared(v) < 1.0f) break;   // random: never NaN
        if (!(--tries > 0)) break;
    }
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

Lanes Expr::RunLanes(const Env& env) const {
    if (scalar || scalarParts) {
        float values[kMaxDepth];    // left unset, as the four-lane stack below
        const int top = RunScalars(code, scalar ? code.size() : code.size() - 1, env, values);
        if (scalar) return Splat(values[top]);
        Lanes joined;
        for (int k = 0; k < scalarParts; ++k) joined.v[k] = values[top - scalarParts + 1 + k];
        return joined;
    }
    // Left unset (each slot is written before it is read): zeroing it costs more than a short run.
    union Stack {
        Stack() {}
        Lanes at[kMaxDepth];
    } slots;
    Lanes* const stack = slots.at;
    int top = -1;
    for (std::size_t pc = 0; pc < code.size();) {
        const Instruction& i = code[pc++];
        switch (i.op) {
        case Op::Constant: stack[++top] = Splat(i.value); break;
        case Op::LoadMeasured:
            if (env.measure) env.measure(env.measureContext);
            [[fallthrough]];
        case Op::Load: {
            const float* scope = env.scopes[static_cast<int>(i.scope)];
            Lanes& out = stack[++top];
            if (!scope) out = Splat(0.0f);
            else if (i.width == 1) out = Splat(scope[i.index]);
            else
                for (int k = 0; k < 4; ++k) out.v[k] = k < i.width ? scope[i.index + k] : 0.0f;
            break;
        }
        case Op::Negate:
            for (float& x : stack[top].v) x = -x;
            break;
        case Op::Not:
            for (float& x : stack[top].v) x = x == 0.0f ? 1.0f : 0.0f;
            break;
        case Op::Truth:
            for (float& x : stack[top].v) x = True(x) ? 1.0f : 0.0f;
            break;
        case Op::Add: Binary(stack, top, i.width, [](float a, float b) { return a + b; }); break;
        case Op::Subtract: Binary(stack, top, i.width, [](float a, float b) { return a - b; }); break;
        case Op::Multiply: Binary(stack, top, i.width, [](float a, float b) { return a * b; }); break;
        case Op::Divide: Binary(stack, top, i.width, [](float a, float b) { return a / b; }); break;
        case Op::Modulo: Binary(stack, top, i.width, [](float a, float b) { return std::fmod(a, b); }); break;
        case Op::Less: Binary(stack, top, i.width, [](float a, float b) { return a < b ? 1.0f : 0.0f; }); break;
        case Op::LessEqual: Binary(stack, top, i.width, [](float a, float b) { return a <= b ? 1.0f : 0.0f; }); break;
        case Op::Greater: Binary(stack, top, i.width, [](float a, float b) { return a > b ? 1.0f : 0.0f; }); break;
        case Op::GreaterEqual: Binary(stack, top, i.width, [](float a, float b) { return a >= b ? 1.0f : 0.0f; }); break;
        case Op::Equal: Binary(stack, top, i.width, [](float a, float b) { return a == b ? 1.0f : 0.0f; }); break;
        case Op::NotEqual: Binary(stack, top, i.width, [](float a, float b) { return a != b ? 1.0f : 0.0f; }); break;
        case Op::Jump: pc = i.index; break;
        case Op::JumpIfFalse:
            if (!True(stack[top--].v[0])) pc = i.index;
            break;
        case Op::AndJump:
            if (!True(stack[top].v[0])) {
                stack[top] = Splat(0.0f);
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::OrJump:
            if (True(stack[top].v[0])) {
                stack[top] = Splat(1.0f);
                pc = i.index;
            } else {
                --top;
            }
            break;
        case Op::Call:
            top -= i.count - 1;
            stack[top] = CallFunction(i, &stack[top], env);
            break;
        case Op::Index:
            stack[top] = Splat(env.tables && i.index < env.tables->size() ? TableAt((*env.tables)[i.index], stack[top].v[0])
                                                                          : 0.0f);
            break;
        case Op::CurveAt:
            stack[top] =
                Splat(env.curves && i.index < env.curves->size() ? (*env.curves)[i.index].At(stack[top].v[0]) : 0.0f);
            break;
        case Op::ListAt:
            stack[top] =
                env.lists && i.index < env.lists->size() ? ListAt((*env.lists)[i.index], stack[top].v[0]) : Splat(0.0f);
            break;
        case Op::AnchorAt: {
            const float number = stack[top].v[0];
            stack[top] = PerLane(i.width, [&](int k) {
                return env.anchorAt ? env.anchorAt(env.anchorContext, i.count, i.scope, i.index + k, number) : 0.0f;
            });
            break;
        }
        case Op::Swizzle: {
            const Lanes from = stack[top];
            stack[top] = PerLane(i.width, [&](int k) { return from.v[(i.index >> (2 * k)) & 3]; });
            break;
        }
        case Op::Construct: {
            top -= i.count - 1;
            Lanes joined = Splat(0.0f);
            int lane = 0;
            for (int part = 0; part < i.count; ++part) {
                const int lanes = (i.index >> (4 * part)) & 15;
                for (int k = 0; k < lanes && lane < 4; ++k) joined.v[lane++] = stack[top + part].v[k];
            }
            stack[top] = joined;
            break;
        }
        }
    }
    return top >= 0 ? stack[top] : Splat(0.0f);
}

float Expr::Run(const Env& env, float fallback) const {
    if (code.empty()) return fallback;
    if (!scalar) return RunLanes(env).v[0];
    float values[kMaxDepth];
    return values[RunScalars(code, code.size(), env, values)];
}

Vector3 Expr::RunVector(const Env& env, const Vector3& fallback) const {
    if (code.empty()) return fallback;
    const Lanes lanes = RunLanes(env);
    return {lanes.v[0], lanes.v[1], lanes.v[2]};
}

Expr Compile(std::string_view text, const Resolver& resolve, std::string* error, bool* loosened) {
    Expr expr;
    Compiler compiler(text, resolve, loosened);
    if (!compiler.Run(&expr)) {
        if (error) *error = compiler.error;
        return {};
    }
    if (expr.code.empty()) {
        if (error) *error = "empty expression";
        return {};
    }
    expr.text = std::string(text);
    Classify(expr);
    return expr;
}

Expr ConstantExpr(float value) {
    Expr expr;
    expr.code.push_back({Op::Constant, Scope::Source, 0, 1, 1, 0, value});
    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    expr.text = text;
    Classify(expr);
    return expr;
}

Expr Joined(const std::vector<Expr>& parts, std::string* error) {
    Expr joined;
    std::uint16_t packed = 0;
    int lanes = 0;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        const Expr& part = parts[k];
        const std::uint16_t offset = static_cast<std::uint16_t>(joined.code.size());
        for (Instruction i : part.code) {
            if (i.op == Op::Jump || i.op == Op::JumpIfFalse || i.op == Op::AndJump || i.op == Op::OrJump)
                i.index = static_cast<std::uint16_t>(i.index + offset);
            joined.code.push_back(i);
        }
        packed = static_cast<std::uint16_t>(packed | part.width << (4 * k));
        lanes += part.width;
        joined.text += (k ? ", " : "") + part.text;
    }
    if (lanes > 4 || parts.size() > 4) {
        if (error) *error = "more than four lanes";
        return {};
    }
    Instruction construct;
    construct.op = Op::Construct;
    construct.count = static_cast<std::uint8_t>(parts.size());
    construct.index = packed;
    construct.width = static_cast<std::uint8_t>(lanes);
    joined.code.push_back(construct);
    joined.width = lanes;
    joined.text = "[" + joined.text + "]";
    Classify(joined);
    return joined;
}

} // namespace effectdata
