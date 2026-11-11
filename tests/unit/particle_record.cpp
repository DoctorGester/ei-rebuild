// The data effects' records without the game (src/engine/particles/particle_expr.h, particle_record.h): expressions
// compiled and run, a record read from TOML and merged over a lower layer's, its problems reported, the state machine
// driven by its typed inputs: stages by ticks, by a condition on an input and by an event, enter and event actions,
// anchors, conditional tick reactions and the stop action; a particle's position written out and its vars after the
// spawn; the features the families of the original effects brought; and every whole effect the notes and the modding guide
// write out read without a problem; a variant built on another effect by `like` (a var or kind put `before` the base's,
// one taken out by `remove`, a chain, a missing base, a circle, a mod over the base); vector values. ei_unit_check
// (unit_check.cpp) runs it.
#include "../../src/engine/particles/particle_record.h"
#include "../../src/engine/core/random.h"
#include "../../src/lib/toml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace effectdata;

int failures = 0;

void Expect(const std::string& what, bool got) {
    if (got) return;
    ++failures;
    std::printf("ei_unit_check: particle record: %s\n", what.c_str());
}

// An expression over two names: x (Source 0) and y (Input 0), tables "t" and a curve "c".
struct Names {
    float source[2] = {3.0f, 0.0f};
    float input[1] = {4.0f};
    std::vector<std::vector<float>> tables{{10.0f, 20.0f, 30.0f}};
    std::vector<Curve> curves{Curve{{0.0f, 1.0f}, {0.0f, 100.0f}}};

    Resolved Resolve(std::string_view name) const {
        if (name == "x") return {Resolved::Kind::Value, Scope::Source, 0, 0.0f};
        if (name == "y") return {Resolved::Kind::Value, Scope::Input, 0, 0.0f};
        if (name == "t") return {Resolved::Kind::Table, Scope::Source, 0, 0.0f};
        if (name == "c") return {Resolved::Kind::Curve, Scope::Source, 0, 0.0f};
        return {};
    }
    Env MakeEnv(IsaacRandom* random) {
        Env env;
        env.scopes[static_cast<int>(Scope::Source)] = source;
        env.scopes[static_cast<int>(Scope::Input)] = input;
        env.tables = &tables;
        env.curves = &curves;
        env.random = random;
        return env;
    }
};

float Run(const std::string& text, Names& names, IsaacRandom* random = nullptr, std::string* error = nullptr) {
    std::string problem;
    const Expr expr = Compile(text, [&names](std::string_view name) { return names.Resolve(name); }, &problem);
    if (error) *error = problem;
    if (expr.Empty()) return std::nanf("");
    return expr.Run(names.MakeEnv(random));
}

void CheckExpressions() {
    Names names;
    Expect("precedence", Run("1 + 2 * x - 8 / 4", names) == 5.0f);
    Expect("parentheses and unary minus", Run("-(x + 1) * -2", names) == 8.0f);
    Expect("comparisons and logic", Run("x > 2 && y <= 4 || 0", names) == 1.0f);
    Expect("not", Run("!(x == 3)", names) == 0.0f);
    Expect("ternary", Run("x < 1 ? 10 : y > 3 ? 20 : 30", names) == 20.0f);
    Expect("modulo", Run("7 % 3", names) == 1.0f);
    Expect("functions", Run("max(min(x, y), 1) + clamp(9, 0, 5) + abs(-2) + floor(1.7)", names) == 11.0f);
    Expect("lerp and pow", Run("lerp(0, 10, 0.5) + pow(2, 3)", names) == 13.0f);
    Expect("constants", std::fabs(Run("sin(pi / 2) + 180 * deg - pi", names) - 1.0f) < 1e-6f);
    Expect("table entries held to the ends", Run("t[1] + t[-5] + t[99]", names) == 60.0f);
    Expect("curve", Run("c(0.25) + c(-1) + c(2)", names) == 125.0f);
    Expect("a negative number is one constant", [] {
        std::string error;
        const Expr expr = Compile("-1.5", {}, &error);
        return expr.IsConstant() && expr.Run({}) == -1.5f;
    }());

    // Short evaluation: the right side's draw happens only when it is needed.
    IsaacRandom a, b;
    Run("0 && range(0, 1) > 2", names, &a);
    Expect("&& draws nothing when the left is false", a.Next() == b.Next());
    IsaacRandom c, d;
    Run("x ? below(1) : range(0, 1)", names, &c);
    d.Next();
    Expect("a ternary draws only its branch", c.Next() == d.Next());
    IsaacRandom e, f;
    const float drawn = Run("range(2, 4)", names, &e);
    Expect("range is IsaacRandom::Range", drawn == f.Range(2.0f, 4.0f));

    // The fast forms (a program of scalars, scalars joined into a vector) answer as the four-lane run, draws included.
    for (const char* text : {"x * 2 + (y > 3 ? range(1, 2) : below(5)) - t[x] % 7", "-x && !y || chance(50) + pick(4)",
                             "max(x, sin(y)) / c(0.5) + atan2(x, 2) + (x != 3)", "vec3(x, range(0, 1), y * 2)"}) {
        std::string problem;
        const Expr fast = Compile(text, [&names](std::string_view name) { return names.Resolve(name); }, &problem);
        Expr lanes = fast;
        lanes.scalar = false;
        lanes.scalarParts = 0;
        IsaacRandom r1, r2;
        const Lanes x = fast.RunLanes(names.MakeEnv(&r1)), y = lanes.RunLanes(names.MakeEnv(&r2));
        Expect(std::string("the fast form of ") + text,
               (fast.scalar || fast.scalarParts == 3) && x.v[0] == y.v[0] && x.v[1] == y.v[1] && x.v[2] == y.v[2] &&
                   x.v[3] == y.v[3] && r1.Next() == r2.Next());
    }
    // A branch past a join of scalars leaves one value (splat), not three: no fast form.
    for (const char* text : {"x ? 255 : vec3(0, 0, 255)", "x ? vec3(0, 0, 255) : 255"}) {
        std::string problem;
        const Expr expr = Compile(text, [&names](std::string_view name) { return names.Resolve(name); }, &problem);
        const Lanes v = expr.RunLanes(names.MakeEnv(nullptr));
        const bool first = text[4] == '2';
        Expect(std::string("a branch past a join: ") + text,
               expr.scalarParts == 0 && v.v[0] == (first ? 255.0f : 0.0f) && v.v[1] == (first ? 255.0f : 0.0f) &&
                   v.v[2] == 255.0f);
    }

    std::string error;
    Run("x + nothing", names, nullptr, &error);
    Expect("an unknown name is refused (" + error + ")", error.find("unknown name 'nothing'") != std::string::npos);
    Run("1 +", names, nullptr, &error);
    Expect("an unfinished expression is refused", !error.empty());
    Run("t", names, nullptr, &error);
    Expect("a table needs its index", error.find("needs an index") != std::string::npos);
    Run("max(1)", names, nullptr, &error);
    Expect("a call with the wrong count is refused", error.find("takes 2") != std::string::npos);
}

constexpr const char* kBase = R"toml(
texture = "Healing"
blend = "additive"

[server]
one_shot = 60

[inputs]
charge = { type = "float", default = 0.25 }
pulse = { type = "event" }
mode = { type = "enum", values = ["calm", "storm"], default = "calm" }
aim = { type = "vector", default = [1, 2, 3] }

[vars]
spin = { start = "1", tick = "spin + 1" }

[emit]
rate = "mode == mode.storm ? 10 : 2"
max = 300

[[stage]]
name = "rise"
ticks = 3
next = "glow"

[[stage]]
name = "glow"
until = "charge >= 1"
next = "burst"
on = { pulse = "flash" }
rate = "1"

[[stage]]
name = "burst"
ticks = 1
next = "glow"
enter = [{ emit = 5, kind = "spark", at = "orb" }, { set = "charge", value = "0" }]

[[stage]]
name = "flash"
ticks = 2
next = "end"

[[on]]
event = "pulse"
do = [{ emit = 2 }]

[[anchor]]
name = "orb"
count = 3
position = { tick = "circle(a.index * pi / 2) * 2 + vec3(0, 0, aim.z)" }

[[particle]]
name = "mote"
lifetime = 20
size = { start = "size * 0.5", tick = "p.size * 0.9" }

[[particle]]
name = "spark"
when = "false"
at = "orb"
lifetime = "range(4, 8)"
vars = { heat = { start = "1", tick = "p.heat * 0.5" } }
alpha = { tick = "p.heat * 255" }
)toml";

constexpr const char* kTop = R"toml(
[emit]
max = 50

[[particle]]
name = "mote"
lifetime = 30

[[particle]]
name = "ember"
when = "chance(10)"
lifetime = 5
)toml";

RawNode Parse(const char* text, const char* file) {
    toml::Document document;
    if (!document.Parse(text, file)) {
        Expect(std::string("parses: ") + document.Error(), false);
        return {};
    }
    return Copy(document.Root(), file);
}

void CheckRecord() {
    RawNode merged = Parse(kBase, "base.effect.toml");
    MergeOver(merged, Parse(kTop, "top.effect.toml"));
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(merged, "Test", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    Expect("the record reads", record != nullptr && errors.empty());
    if (!record) return;
    Expect("the id is in the data range", IsDataId(record->id) && record->id == IdOf("test") && record->id == IdOf("TEST"));
    Expect("the top layer's emit max over the base's", record->maxParticles == 50);
    Expect("the base's emit rate kept", record->rate.text == "mode == mode.storm ? 10 : 2");
    Expect("a particle merged by name and one appended", record->particles.size() == 3 &&
                                                            record->particles[0].name == "mote" &&
                                                            record->particles[0].lifetime.Run({}) == 30.0f &&
                                                            record->particles[0].sizeTick.text == "p.size * 0.9" &&
                                                            record->particles[2].name == "ember");
    Expect("the inputs' slots", record->inputValues == 6 && record->FindInput("aim")->slot == 3);
    Expect("the server block", record->oneShot == 60 && record->scriptLifetime == -1 && record->additive);

    // Problems: an unknown key is noted, an unknown name makes the record unusable.
    RawNode broken = Parse(kBase, "base.effect.toml");
    MergeOver(broken, Parse("colour = 1\n[[particle]]\nname = \"mote\"\nalpha = \"p.nothing\"\n", "bad.effect.toml"));
    errors.clear();
    Expect("an unknown name refuses the effect", Read(broken, "Bad", &errors) == nullptr);
    bool key = false, name = false;
    for (const std::string& error : errors) {
        key = key || error.find("bad.effect.toml:1: unknown key 'colour'") != std::string::npos;
        name = name || (error.find("bad.effect.toml:4") != std::string::npos &&
                        error.find("unknown name 'p.nothing'") != std::string::npos);
    }
    Expect("the unknown key named with its file and line", key);
    Expect("the unknown name named with its file and line", name);
}

void CheckState() {
    std::vector<std::string> errors;
    RawNode root = Parse(kBase, "base.effect.toml");
    const std::shared_ptr<const Record> record = Read(root, "Test", &errors);
    if (!record) return Expect("the base record reads", false);
    State state(record);
    IsaacRandom random;
    auto stageName = [&] { return state.stage >= 0 ? record->stages[state.stage].name : std::string("-"); };

    state.Step(random);
    Expect("the first step starts in the first stage", stageName() == "rise" && state.rate == 2.0f && state.emitting);
    Expect("vars start", state.vars[0] == 1.0f);
    Expect("three anchors on their circle at the vector input's height",
           state.anchors.size() == 3 && state.source[kSourceValues] == 3.0f &&
               std::fabs(state.anchors[1].values[static_cast<int>(AnchorValue::PositionY)] - 2.0f) < 1e-6f &&
               state.anchors[2].values[static_cast<int>(AnchorValue::PositionZ)] == 3.0f);
    state.Step(random);
    state.Step(random);
    Expect("vars tick", state.vars[0] == 3.0f);
    Expect("still rising before its ticks are up", stageName() == "rise");
    state.Step(random);
    Expect("its ticks up: the next stage, with its own rate", stageName() == "glow" && state.rate == 1.0f);

    Expect("an enum input by its value's name", state.SetEnum("mode", "storm") && !state.SetEnum("mode", "gale"));
    Expect("a float input", state.SetValue("charge", 1.5f) && !state.SetValue("pulse", 1.0f));
    state.Step(random);
    Expect("until holds: the burst, its enter actions queued and an input set",
           stageName() == "burst" && state.queue.size() == 15 && state.queue[0].kind == 1 && state.queue[0].anchor == 0 &&
               state.inputs[0] == 0.0f);
    Expect("the storm's rate (the burst stage has none of its own)", state.rate == 10.0f);
    state.Step(random);
    Expect("back to glow after its one tick", stageName() == "glow" && state.queue.empty());

    Expect("an event fires", state.Fire("pulse") && !state.Fire("charge"));
    state.Step(random);
    Expect("the event's reaction (a burst of regular tries) and its stage move",
           stageName() == "flash" && state.burst == 2 && state.inputs[record->FindInput("pulse")->slot] == 1.0f);
    state.Step(random);
    Expect("an event lasts one tick", state.inputs[record->FindInput("pulse")->slot] == 0.0f);
    state.Step(random);
    Expect("next = end ends the effect", state.ended && !state.emitting);

    Expect("the vector input", state.SetVector("aim", 0.0f, 0.0f, 7.0f) && !state.SetVector("charge", 0, 0, 0));
}

constexpr const char* kTicking = R"toml(
texture = "Stun"
flags = ["keep_alive"]
[inputs]
armed = { type = "bool" }
[[on]]
event = "tick"
when = "armed && count > 0"
do = [{ stop = true }]
[[on]]
event = "tick"
when = "age == 1"
do = [{ set = "height", value = "exp(log(4) * 2) + wind.z" }, { stop_emitting = true }]
[[particle]]
name = "star"
lifetime = 3
size = "emitting ? carrier_radius : 0"
)toml";

int g_measures = 0;
void CountMeasure(void*) { ++g_measures; }

void CheckReactions() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kTicking, "ticking.effect.toml"), "Ticking", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the ticking record reads", false);
    Expect("keep_alive and a read of the carrier noted", record->keepAlive && record->readsCarrier);
    const std::shared_ptr<const Record> original = Read(Parse(kTicking, "ticking.effect.toml"), "Ticking", &errors, 0x2041);
    Expect("an original type's id given", original && original->id == 0x2041);
    State state(record);
    state.measure = CountMeasure;
    IsaacRandom random;
    state.Step(random);
    Expect("a tick reaction waits for its condition", !state.stopRequested);
    const Expr& starSize = record->particles[0].sizeStart;
    Expect("the carrier not measured where it is not read",
           starSize.Run(state.SourceEnv(&random)) == 0.0f && g_measures == 0);
    state.source[static_cast<int>(SourceValue::Emitting)] = 1.0f;
    starSize.Run(state.SourceEnv(&random));
    Expect("the carrier measured at its read", g_measures == 1);
    state.source[static_cast<int>(SourceValue::WindZ)] = 0.5f;
    state.SetValue("armed", 1.0f);
    state.Step(random);
    Expect("its condition reads the source's values (no particles yet)", !state.stopRequested);
    Expect("a source value set (exp, log, wind.z) and the emission flag cleared",
           state.sourceWrites.size() == 1 && state.sourceWrites[0] == static_cast<int>(SourceValue::Height) &&
               std::fabs(state.source[static_cast<int>(SourceValue::Height)] - 16.5f) < 0.001f && state.stopEmitting);
    state.source[static_cast<int>(SourceValue::Particles)] = 2.0f;
    state.Step(random);
    Expect("the condition holds: the stop asked for, the effect goes on",
           state.stopRequested && !state.ended && state.emitting);
}

constexpr const char* kPlaced = R"toml(
texture = "Stun"
[[particle]]
name = "spark"
lifetime = 7
vars = { a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, reach = "below(0.6)", dx = "p.spawn.x * p.reach" }
spawn = "ball()"
position = { start = "origin + vec3(p.dx, 0, 0)", tick = "p.position + side * spread_min.x + vec3(0, up.y, spread_max.z)" }
)toml";

void CheckPlacement() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kPlaced, "placed.effect.toml"), "Placed", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the placed record reads", false);
    const ParticleKind& kind = record->particles[0];
    Expect("eight vars", kind.vars.size() == 8 && record->particles[0].vars[7].start.text == "p.spawn.x * p.reach");
    Expect("the position's start and tick", kind.position.text == "origin + vec3(p.dx, 0, 0)" &&
                                                kind.position.width == 3 && kind.positionTick.width == 3 &&
                                                kind.positionTick.text ==
                                                    "p.position + side * spread_min.x + vec3(0, up.y, spread_max.z)");
}

// MF15g3's features: a var set by an "emit" reaction, let_go, at_create, a numbered anchor, a particle's source writes,
// drop, the carrier's unit values.
constexpr const char* kCarrierReader = R"toml(
texture = "Stun"
[vars]
gusts = { start = "pick(5) + 4", at_create = true }
tries = 1
[[on]]
event = "emit"
when = "carrier_unit"
do = [{ set = "tries", value = "carrier_running ? 4 : 1" }, { let_go = true }]
[[anchor]]
name = "gust"
count = "gusts"
vars.reach = 2
[[particle]]
name = "leaf"
at = "gust"
anchor_index = "p.gust"
lifetime = 5
vars = { gust = "pick(anchors.gust)" }
spawn = "disc()"
drop = "!carrier_unit"
source = { size = "size * 0.75", alpha = 0 }
position = { tick = "vec3(carrier_left_foot.x, carrier_axis.y, carrier_bounds_radius + carrier_dead)" }
)toml";

constexpr const char* kEmitsInEmit = R"toml(
texture = "Stun"
[[on]]
event = "emit"
do = [{ emit = 1 }]
[[particle]]
name = "leaf"
lifetime = 5
)toml";

constexpr const char* kEarlyAnchor = R"toml(
texture = "Stun"
[[anchor]]
name = "gust"
count = 2
vars.reach = 2
[[particle]]
name = "leaf"
at = "gust"
anchor_index = 1
lifetime = 5
vars = { reach = "a.reach" }
)toml";

void CheckCarrierReaders() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kCarrierReader, "reader.effect.toml"), "Reader", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the carrier reader reads", false);
    const ParticleKind& kind = record->particles[0];
    Expect("the carrier's unit values noted", record->readsUnit && !record->readsCarrier);
    Expect("a numbered anchor, a drop, a spawn and two source writes",
           !kind.anchorIndex.Empty() && !kind.drop.Empty() && kind.spawn.width == 3 &&
               kind.writes.size() == 2 && kind.writes[0].index == static_cast<int>(SourceValue::Size) &&
               kind.writes[1].index == static_cast<int>(SourceValue::Alpha));
    Expect("an at_create var", record->vars.size() == 2 && record->vars[0].atCreate && !record->vars[1].atCreate);
    State state(record);
    IsaacRandom random;
    state.Create(random);
    const float gusts = state.vars[0];
    Expect("the at_create var drawn when the source is made", gusts >= 4.0f && gusts <= 8.0f);
    state.Step(random);
    Expect("the first tick keeps it and starts the others", state.vars[0] == gusts && state.vars[1] == 1.0f &&
                                                                state.anchors.size() == static_cast<std::size_t>(gusts));
    state.source[static_cast<int>(SourceValue::CarrierUnit)] = 1.0f;
    state.source[static_cast<int>(SourceValue::CarrierRunning)] = 1.0f;
    state.Emitted(random);
    Expect("an emit reaction sets a var and lets go", state.vars[1] == 4.0f && state.letGo);
    errors.clear();
    Expect("an emit reaction that emits refuses the effect",
           Read(Parse(kEmitsInEmit, "emits.effect.toml"), "Emits", &errors) == nullptr && errors.size() == 1);
    errors.clear();
    Expect("a numbered anchor read before it is chosen refuses the effect",
           Read(Parse(kEarlyAnchor, "early_anchor.effect.toml"), "EarlyAnchor", &errors) == nullptr &&
               errors.size() == 1 && errors[0].find("anchor_index") != std::string::npos);
}

// MF15j's extensions: anchors added by a "before_emit" reaction with a ball spawn, `ground` of a point, a list input,
// `pick = "first"`, a spawn between two points, on_death actions.
constexpr const char* kExtended = R"toml(
texture = "Stun"
[inputs]
path = { type = "list", default = [[1, 2, 3], [4, 5, 6]] }
[vars]
made = 0
deaths = 0
[[on]]
event = "before_emit"
when = "anchors.ray == 0"
do = [{ add_anchor = "ray", count = "path.count + 1" }, { set = "made", value = "ground(vec2(path[1].x, path[9].y))" }]
[[anchor]]
name = "ray"
count = 0
spawn = "ball()"
position = "vec3(normalize(a.spawn).x, a.index, a.count)"
[[particle]]
name = "spark"
at = "ray"
pick = "first"
lifetime = 4
spawn = "lerp(vec3(1, 0, 0), vec3(path[0].z, 0, 0), range(0, 1))"
on_death = [{ emit = "p.age + 1", kind = "ember" }, { effect = "Sparks" }, { set = "deaths", value = "deaths + 1" }]
[[particle]]
name = "ember"
when = false
lifetime = 2
)toml";

constexpr const char* kDeathStops = R"toml(
texture = "Stun"
[[particle]]
name = "spark"
lifetime = 4
on_death = [{ stop = true }]
)toml";

constexpr const char* kBeforeEmitEmits = R"toml(
texture = "Stun"
[[on]]
event = "before_emit"
do = [{ emit = 1 }]
[[particle]]
name = "spark"
lifetime = 4
)toml";

float Ground(float x, float y) { return x * 10.0f + y; }

void CheckExtensions() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kExtended, "extended.effect.toml"), "Extended", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the extended record reads", false);
    const ParticleKind& spark = record->particles[0];
    Expect("before_emit noted, pick first, a spawn, three on_death actions",
           record->beforeEmit && spark.pick == AnchorPick::First && spark.spawn.width == 3 &&
               spark.onDeath.size() == 3 && record->lists == 1);
    State state(record);
    state.ground = Ground;
    IsaacRandom random;
    state.Step(random);
    Expect("an anchor group of count 0 makes none at the start", state.anchors.empty() && state.source[kSourceValues] == 0.0f);
    IsaacRandom expected = random;
    state.BeforeEmission(random);
    Expect("before_emit adds path.count + 1 anchors", state.anchors.size() == 3 && state.source[kSourceValues] == 3.0f);
    Expect("ground of a point over the list's held points (path[1].x = 4, path[9].y = 5)", state.vars[0] == 45.0f);
    bool drawn = true;
    for (const AnchorState& anchor : state.anchors) {
        const Vector3 v = RandomInBall(expected);
        const float* values = anchor.values;
        drawn = drawn && values[static_cast<int>(AnchorValue::SpawnX)] == v.x &&
                values[static_cast<int>(AnchorValue::SpawnZ)] == v.z &&
                std::fabs(values[static_cast<int>(AnchorValue::PositionX)] - v.x / Length(v)) < 1e-6f;
    }
    Expect("each added anchor draws its ball in turn, its x reads a.spawn", drawn && random.Next() == expected.Next());
    Expect("a.index counts the group, a.count is its size",
           state.anchors[2].values[static_cast<int>(AnchorValue::PositionY)] == 2.0f &&
               state.anchors[0].values[static_cast<int>(AnchorValue::PositionZ)] == 3.0f);
    state.BeforeEmission(random);
    Expect("its condition then keeps it from adding more", state.anchors.size() == 3);
    Expect("a list input set from game code", state.SetList("path", {{7.0f, 8.0f, 9.0f}}) && !state.SetList("made", {}) &&
                                                  state.inputs[record->FindInput("path")->slot] == 1.0f);

    // The spawn: one draw between two points.
    IsaacRandom a, b;
    const Vector3 point = spark.spawn.RunVector(state.SourceEnv(&a), {});
    const float t = b.Range(0.0f, 1.0f);
    Expect("a spawn from 1 to path[0].z (9)", point.x == 1.0f + 8.0f * t && point.y == 0.0f && a.Next() == b.Next());

    // A death: the particle's age in its count, the ember queued at its place, the effect there, the var set.
    float particle[kParticleValues] = {};
    particle[static_cast<int>(ParticleValue::Age)] = 2.0f;
    Env env = state.SourceEnv(&random);
    env.scopes[static_cast<int>(Scope::Particle)] = particle;
    state.queue.clear();
    state.Died(spark, env, {5.0f, 6.0f, 7.0f});
    Expect("on_death: three embers at the place, the effect, the var",
           state.queue.size() == 3 && state.queue[0].kind == 1 && state.queue[0].placed &&
               state.queue[2].place.y == 6.0f && state.effects.size() == 1 && state.effects[0].placed &&
               state.effects[0].name == "Sparks" && state.vars[1] == 1.0f);

    errors.clear();
    Expect("an on_death that stops refuses the effect",
           Read(Parse(kDeathStops, "death.effect.toml"), "Death", &errors) == nullptr && errors.size() == 1 &&
               errors[0].find("on_death") != std::string::npos);
    errors.clear();
    Expect("a before_emit reaction that emits refuses the effect",
           Read(Parse(kBeforeEmitEmits, "before.effect.toml"), "Before", &errors) == nullptr && errors.size() == 1);
}

// MF15g4's: an "emit" reaction that emits a kind, a shake action, the platform functions.
constexpr const char* kReactionEmits = R"toml(
texture = "Stun"
[vars]
plane = "platform_height(vec2(1, 2)) + platform_normal(vec2(1, 2)).z * 10 + platform_normal(vec2(1, 2)).x * 100"
[[on]]
event = "emit"
when = "count == 0"
do = [{ emit = "2 + 1", kind = "puff" }, { shake = "size * 2", time = 5, decay = 0.25 }]
[[particle]]
name = "try"
lifetime = 1
drop = true
[[particle]]
name = "puff"
when = false
lifetime = 4
)toml";

constexpr const char* kEmitReactionBurst = R"toml(
texture = "Stun"
[[on]]
event = "emit"
do = [{ emit = 1 }]
[[particle]]
name = "spark"
lifetime = 4
)toml";

constexpr const char* kShakeWithoutTime = R"toml(
texture = "Stun"
[[on]]
event = "start"
do = [{ shake = 1, decay = 0.25 }]
[[particle]]
name = "spark"
lifetime = 4
)toml";

float Platform(float x, float y, int which) { return which == 0 ? x + y : which == 3 ? 0.5f : 0.0f; }

void CheckReactionParticles() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kReactionEmits, "reaction.effect.toml"), "Reaction", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the reaction record reads", false);
    State unplaced(record);
    IsaacRandom random;
    unplaced.Step(random);
    Expect("platform_* without a platform: height 0, normal (0, 0, 1)", unplaced.vars[0] == 10.0f);
    State state(record);
    state.platform = Platform;
    state.source[static_cast<int>(SourceValue::Size)] = 1.5f;
    state.Step(random);
    Expect("platform_height and platform_normal through State::platform", state.vars[0] == 8.0f);
    state.Emitted(random);
    Expect("an \"emit\" reaction queues its kind and a shake",
           state.queue.size() == 3 && state.queue[0].kind == 1 && state.queue[0].anchor == -1 &&
               state.shakes.size() == 1 && state.shakes[0].strength == 3.0f && state.shakes[0].time == 5.0f &&
               state.shakes[0].decay == 0.25f);
    state.Step(random);
    Expect("the step clears the shakes", state.shakes.empty());

    errors.clear();
    Expect("an \"emit\" reaction's emit without a kind refuses the effect",
           Read(Parse(kEmitReactionBurst, "burst.effect.toml"), "Burst", &errors) == nullptr && errors.size() == 1 &&
               errors[0].find("emit with kind") != std::string::npos);
    errors.clear();
    Expect("a shake without its time refuses the effect",
           Read(Parse(kShakeWithoutTime, "shake.effect.toml"), "Shake", &errors) == nullptr && errors.size() == 1);
}

// The casting family's: ball(), an anchor group's values by number, a particle's writes to an effect var and its
// anchor's vars, a jump, carrier_unit_radius.
constexpr const char* kCastFeatures = R"toml(
texture = "Stun"
[vars]
b = "ball()"
seen = 0
radius = "carrier_unit_radius * 2"
[[on]]
event = "tick"
do = [{ set = "seen", value = "orb.v[1] + orb.v[9] * 10 + orb.index[-3] * 100" }]
[[anchor]]
name = "orb"
count = 3
spawn = "ball()"
vars.v = "a.index * 2 + 1"
[[particle]]
name = "wisp"
at = "orb"
lifetime = 4
spawn = "shell()"
source = { size = 2, seen = "p.age", a.v = 7 }
jump = { when = "p.age == 1", size = 3 }
[[particle]]
name = "plain"
when = false
lifetime = 4
jump = "p.age == 2"
)toml";

constexpr const char* kBareGroup = R"toml(
texture = "Stun"
[vars]
seen = "orb.v"
[[anchor]]
name = "orb"
count = 1
vars.v = 1
[[particle]]
name = "wisp"
lifetime = 4
)toml";

constexpr const char* kWriteMissing = R"toml(
texture = "Stun"
[[anchor]]
name = "orb"
count = 1
vars.v = 1
[[particle]]
name = "wisp"
at = "orb"
lifetime = 4
source = { a.missing = 1 }
)toml";

constexpr const char* kJumpWithoutWhen = R"toml(
texture = "Stun"
[[particle]]
name = "wisp"
lifetime = 4
jump = { size = 1 }
)toml";

void CheckCastFeatures() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kCastFeatures, "cast.effect.toml"), "Cast", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the cast features record reads", false);
    const ParticleKind& wisp = record->particles[0];
    using To = ParticleKind::Write::To;
    Expect("source writes to the source, an effect var and an anchor var, in the file's order",
           wisp.writes.size() == 3 && wisp.writes[0].to == To::Source &&
               wisp.writes[0].index == static_cast<int>(SourceValue::Size) && wisp.writes[1].to == To::Var &&
               wisp.writes[1].index == 3 && wisp.writes[2].to == To::AnchorVar && wisp.writes[2].index == 0);
    Expect("a jump with its size, and one by its condition alone",
           !wisp.jump.Empty() && !wisp.jumpSize.Empty() && !record->particles[1].jump.Empty() &&
               record->particles[1].jumpSize.Empty());

    State state(record);
    state.source[static_cast<int>(SourceValue::CarrierUnitRadius)] = 1.25f;
    IsaacRandom random;
    IsaacRandom expected = random;
    state.Step(random);
    const Vector3 ball = RandomInBall(expected);
    Expect("ball() draws a point in the ball", state.vars[0] == ball.x && state.vars[1] == ball.y && state.vars[2] == ball.z);
    Expect("carrier_unit_radius reads the source's value", state.vars[4] == 2.5f);
    state.Step(random);
    Expect("orb.v[i] by the anchor's number, held to the group's ends (3 + 5 * 10 + 0 * 100)", state.vars[3] == 53.0f);

    errors.clear();
    Expect("a group's var without the anchor's number refuses the effect",
           Read(Parse(kBareGroup, "bare.effect.toml"), "Bare", &errors) == nullptr && errors.size() == 1 &&
               errors[0].find("anchor's number") != std::string::npos);
    errors.clear();
    Expect("a write to an anchor var the group lacks refuses the effect",
           Read(Parse(kWriteMissing, "missing.effect.toml"), "Missing", &errors) == nullptr && errors.size() == 1);
    errors.clear();
    Expect("a jump table without its when refuses the effect",
           Read(Parse(kJumpWithoutWhen, "jump.effect.toml"), "Jump", &errors) == nullptr && errors.size() == 1);
}

// MF15g6's: a "spawn" reaction with a scorch and a shake, anchors made with the source (at_create) and their spawns,
// `surface` of a point, the sun's colour, the frame mode `free`.
constexpr const char* kBlastFeatures = R"toml(
texture = "Stun"
[vars]
stand = "surface(vec2(3, 4))"
light = "sun.r + sun.g * 10"
[[on]]
event = "spawn"
do = [{ scorch = "param * 2", alpha = "alpha * 0.5" }, { shake = 2, time = "base_size", decay = 0.25 }, { scorch = 1 }]
[[anchor]]
name = "fragment"
count = 4
at_create = true
spawn = "ball()"
vars.n = { start = 0, tick = "a.n + 1" }
[[particle]]
name = "flame"
lifetime = 4
frame = { start = 1, tick = "p.frame + 9", mode = "free" }
)toml";

constexpr const char* kSpawnEmits = R"toml(
texture = "Stun"
[[on]]
event = "spawn"
do = [{ emit = 1 }]
[[particle]]
name = "flame"
lifetime = 4
)toml";

float SumGround(float x, float y) { return x + y; }
float Standing(float, float, int which) { return which == 4 ? 9.0f : 0.0f; }

void CheckBlastFeatures() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kBlastFeatures, "blast.effect.toml"), "Blast", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the blast features record reads", false);
    Expect("a spawn reaction marks the record", record->spawns && record->reactions[0].on == Reaction::On::Spawn);
    Expect("frame mode free reads", record->particles[0].frameMode == FrameMode::Free);

    State state(record);
    state.ground = SumGround;
    state.source[static_cast<int>(SourceValue::Param)] = 1.5f;
    state.source[static_cast<int>(SourceValue::Alpha)] = 0.8f;
    state.source[static_cast<int>(SourceValue::BaseSize)] = 3.0f;
    state.source[static_cast<int>(SourceValue::SunR)] = 0.5f;
    state.source[static_cast<int>(SourceValue::SunG)] = 0.25f;
    IsaacRandom random;
    IsaacRandom expected = random;
    state.Create(random);
    Expect("at_create anchors are made with the source", state.anchors.size() == 4);
    const Vector3 first = RandomInBall(expected);
    Expect("an at_create anchor draws its spawn when the source is made",
           state.anchors[0].values[static_cast<int>(AnchorValue::SpawnZ)] == first.z &&
               state.anchors[0].values[static_cast<int>(AnchorValue::SpawnX)] == first.x);
    state.Spawned(random);
    Expect("a spawn reaction asks for its scorches and its shake",
           state.scorches.size() == 2 && state.scorches[0].radius == 3.0f && state.scorches[0].alpha == 0.4f &&
               state.scorches[1].radius == 1.0f && state.scorches[1].alpha == 1.0f && state.shakes.size() == 1 &&
               state.shakes[0].strength == 2.0f && state.shakes[0].time == 3.0f);
    state.Step(random);
    Expect("the step clears the scorches", state.scorches.empty());
    Expect("anchors made with the source tick at the first step",
           state.anchors.size() == 4 && state.anchors[3].vars[0] == 1.0f &&
               state.anchors[0].values[static_cast<int>(AnchorValue::Age)] == 1.0f);
    Expect("surface of a point is the ground without a platform callback", state.vars[0] == 7.0f);
    Expect("sun.r, sun.g read the source's values", state.vars[1] == 3.0f);
    State standing(record);
    standing.platform = Standing;
    standing.Step(random);
    Expect("surface of a point through State::platform (4)", standing.vars[0] == 9.0f);

    errors.clear();
    Expect("a spawn reaction that emits refuses the effect",
           Read(Parse(kSpawnEmits, "spawn.effect.toml"), "Spawn", &errors) == nullptr && errors.size() == 1 &&
               errors[0].find("spawn") != std::string::npos);
}

// The ```toml blocks of `file` (relative to the source tree) that are whole effects (a [[particle]]).
std::vector<std::string> EffectBlocks(const char* file) {
    std::ifstream in(std::string(EI_SOURCE_DIR) + "/" + file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    const std::string all = text.str();
    Expect(std::string("reads ") + file, !all.empty());
    std::vector<std::string> blocks;
    for (std::size_t at = all.find("```toml"); at != std::string::npos; at = all.find("```toml", at)) {
        at = all.find('\n', at);   // the fence's line, whatever its line end
        if (at == std::string::npos) break;
        const std::size_t end = all.find("```", at);
        if (end == std::string::npos) break;
        std::string block = all.substr(at, end - at);
        if (block.find("[[particle]]") != std::string::npos) blocks.push_back(std::move(block));
        at = end + 3;
    }
    return blocks;
}

void CheckWrittenEffects() {
    int read = 0;
    for (const char* file : {"docs/notes/effects.md", "docs/modding.md"}) {
        int index = 0;
        for (const std::string& block : EffectBlocks(file)) {
            const std::string name = std::string(file) + " #" + std::to_string(++index);
            std::vector<std::string> errors;
            const std::shared_ptr<const Record> record = Read(Parse(block.c_str(), name.c_str()), "Written", &errors);
            for (const std::string& error : errors) std::printf("ei_unit_check: particle record: %s\n", error.c_str());
            Expect(name + " reads without a problem", record != nullptr && errors.empty());
            ++read;
        }
    }
    Expect("the guide's effects found", read >= 1);
}

// `like`: a variant read over its base's resolved tree, a chain, a missing base, a circle, and a mod's layer over the
// base that the variant takes too.
void CheckLike() {
    const std::vector<std::pair<std::string, std::string>> files = {
        {"Ring", R"toml(
texture = "A"
[emit]
rate = 2
[vars]
spin = 1
lift = 2
[tables]
fade = [1, 2, 3]
[[on]]
event = "tick"
when = "spin > 0"
do = [{ set = "spin", value = "spin + 1" }, { set = "lift", value = 0 }]
[[particle]]
name = "mote"
lifetime = 20
size = { start = 0.5, tick = "p.size * 0.9" }
[[particle]]
name = "spark"
lifetime = 4
)toml"},
        {"Ring2", R"toml(
like = "Ring"
texture = "B"
[vars]
lift = 5
[tables]
fade = [7]
[[on]]
event = "tick"
do = [{ stop = true }]
[[particle]]
name = "spark"
lifetime = 9
[[particle]]
name = "ember"
lifetime = 3
)toml"},
        {"Ring3", "like = \"ring2\"\n[vars]\nspin = 3\n"},
        {"Ring4", R"toml(
like = "Ring"
[vars]
early = { start = 4, before = "spin" }
lift = { remove = true }
[[on]]
event = "tick"
remove = true
[[particle]]
name = "first"
before = "mote"
lifetime = 1
[[particle]]
name = "spark"
remove = true
)toml"},
        {"Lost", "like = \"Nowhere\"\n"},
        {"Self", "like = \"Self\"\n"},
        {"Ping", "like = \"Pong\"\n"},
        {"Pong", "like = \"Ping\"\n"},
        {"Odd", "like = 3\n"},
    };
    std::string modRate;   // a mod's layer over Ring: [emit] rate = ...
    auto layers = [&](std::string_view effect, RawNode* tree) {
        for (const auto& [name, text] : files) {
            if (name.size() != effect.size() ||
                !std::equal(name.begin(), name.end(), effect.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }))
                continue;
            *tree = Parse(text.c_str(), (name + ".effect.toml").c_str());
            if (name == "Ring" && !modRate.empty())
                MergeOver(*tree, Parse(("[emit]\nrate = " + modRate + "\n").c_str(), "mod/Ring.effect.toml"));
            return true;
        }
        return false;
    };
    auto resolve = [&](const std::string& name, std::vector<std::string>* errors) -> std::shared_ptr<const Record> {
        RawNode tree;
        if (!layers(name, &tree)) return nullptr;
        if (!ResolveLike(tree, name, layers, errors)) return nullptr;
        return Read(tree, name, errors);
    };
    auto var = [](const Record& record, std::string_view name) -> float {
        for (const Var& v : record.vars)
            if (v.name == name) return v.start.Run({});
        return -1.0f;
    };

    std::vector<std::string> errors;
    const std::shared_ptr<const Record> ring2 = resolve("Ring2", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    Expect("a variant reads over its base", ring2 != nullptr && errors.empty());
    if (ring2) {
        Expect("the variant's texture and var replace the base's, the others kept",
               ring2->texture == "B" && var(*ring2, "lift") == 5.0f && var(*ring2, "spin") == 1.0f &&
                   ring2->vars.size() == 2 && ring2->vars[0].name == "spin");
        Expect("the base's emission kept", ring2->rate.Run({}) == 2.0f);
        Expect("a number array replaced whole", ring2->tables.size() == 1 && ring2->tables[0].size() == 1 &&
                                                     ring2->tables[0][0] == 7.0f);
        Expect("a particle kind merged by name, a new one appended",
               ring2->particles.size() == 3 && ring2->particles[0].name == "mote" &&
                   ring2->particles[0].lifetime.Run({}) == 20.0f && ring2->particles[1].name == "spark" &&
                   ring2->particles[1].lifetime.Run({}) == 9.0f && ring2->particles[2].name == "ember");
        Expect("an [[on]] merged by its event: its when kept, its action list replaced whole",
               ring2->reactions.size() == 1 && ring2->reactions[0].when.text == "spin > 0" &&
                   ring2->reactions[0].actions.size() == 1);
        Expect("the variant's own name, id and file", ring2->name == "Ring2" && ring2->id == IdOf("ring2") &&
                                                           ring2->file == "Ring2.effect.toml");
    }

    errors.clear();
    const std::shared_ptr<const Record> ring3 = resolve("Ring3", &errors);
    Expect("a chain (Ring3 like Ring2 like Ring) reads", ring3 != nullptr && errors.empty());
    if (ring3)
        Expect("a chain takes every level's fields", ring3->texture == "B" && var(*ring3, "spin") == 3.0f &&
                                                          var(*ring3, "lift") == 5.0f && ring3->particles.size() == 3);

    errors.clear();
    const std::shared_ptr<const Record> ring4 = resolve("Ring4", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    Expect("before and remove read", ring4 != nullptr && errors.empty());
    if (ring4) {
        Expect("a var put before the base's and one taken out",
               ring4->vars.size() == 2 && ring4->vars[0].name == "early" && var(*ring4, "early") == 4.0f &&
                   ring4->vars[1].name == "spin");
        Expect("a kind put before the base's and one taken out, an [[on]] taken out",
               ring4->particles.size() == 2 && ring4->particles[0].name == "first" &&
                   ring4->particles[1].name == "mote" && ring4->reactions.empty());
    }

    modRate = "7";
    errors.clear();
    const std::shared_ptr<const Record> modded = resolve("Ring3", &errors);
    Expect("a mod's layer over the base changes its variants", modded != nullptr && modded->rate.Run({}) == 7.0f);
    modRate.clear();

    auto refused = [&](const std::string& name, std::string_view where, std::string_view what) {
        errors.clear();
        const bool none = resolve(name, &errors) == nullptr;
        bool said = false;
        for (const std::string& error : errors)
            said = said || (error.find(where) != std::string::npos && error.find(what) != std::string::npos);
        Expect(name + ": refused with " + std::string(what) + " at " + std::string(where), none && said);
    };
    refused("Lost", "Lost.effect.toml:1", "no effect of that name");
    refused("Self", "Self.effect.toml:1", "goes round in a circle (self -> self)");
    refused("Ping", "Pong.effect.toml:1", "goes round in a circle (ping -> pong -> ping)");
    refused("Odd", "Odd.effect.toml:1", "like takes the name of another effect");

    // Without `like` the tree is left as it is.
    RawNode plain = Parse("texture = \"A\"\n", "plain.effect.toml");
    errors.clear();
    Expect("no like: nothing to resolve",
           ResolveLike(plain, "Plain", layers, &errors) && errors.empty() && plain["texture"].text == "A");
}

// Vector values: a vector input and vars of three lanes, vec3, swizzles, the vector functions, a var that reads a var
// declared after it (the reader's second pass widens that one), one whose expression swizzles itself (the loose width
// passes widen it), ball(), and the refusals.
constexpr const char* kVectors = R"toml(
texture = "Stun"
[inputs]
aim = { type = "vector", default = [3, 4, 0] }
[vars]
early = "later.z + 1"
dir = "normalize(aim)"
far = "length(aim) + dot(aim, vec3(1, 0, 0))"
turned = "cross(vec3(1, 0, 0), vec3(0, 1, 0)).z + aim.yx.x"
point = "ball()"
mid = "lerp(vec3(0), vec3(10, 20, 30), 0.5) + dir.y"
later = "vec3(1, 2, 3)"
grows = { tick = "vec3(grows.xy + vec2(1, 2), 3)" }
[[particle]]
name = "spark"
lifetime = 1
)toml";

constexpr const char* kNarrowSwizzle = R"toml(
texture = "Stun"
[vars]
s = 1
v = "s.xy"
[[particle]]
name = "spark"
lifetime = 1
)toml";

constexpr const char* kWideParticle = R"toml(
texture = "Stun"
[[particle]]
name = "spark"
lifetime = 1
vars = { a = "vec3(1)", b = "vec3(2)", c = "vec3(3)" }
)toml";

void CheckVectors() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kVectors, "vectors.effect.toml"), "Vectors", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the vector record reads", false);
    State state(record);
    IsaacRandom random, fresh;
    state.Step(random);
    auto find = [&](std::string_view name) {
        for (const Var& v : record->vars)
            if (v.name == name) return &v;
        return static_cast<const Var*>(nullptr);
    };
    auto lane = [&](std::string_view name, int k = 0) {
        const Var* v = find(name);
        return v && k < v->width ? state.vars[v->slot + k] : std::nanf("");
    };
    auto width = [&](std::string_view name) { return find(name) ? find(name)->width : 0; };
    Expect("vars take their expressions' lanes", width("dir") == 3 && width("far") == 1 && width("point") == 3 &&
                                                     width("later") == 3 && width("grows") == 3 &&
                                                     record->varFloats == 18);
    Expect("normalize", std::fabs(lane("dir", 0) - 0.6f) < 1e-6f && std::fabs(lane("dir", 1) - 0.8f) < 1e-6f);
    Expect("length and dot", lane("far") == 8.0f);
    Expect("cross and a swizzle", lane("turned") == 5.0f);
    const Vector3 ball = RandomInBall(fresh);
    Expect("ball() is RandomInBall", lane("point", 0) == ball.x && lane("point", 1) == ball.y && lane("point", 2) == ball.z);
    Expect("a per-lane lerp plus a scalar", std::fabs(lane("mid", 2) - 15.8f) < 1e-5f);
    Expect("a var reads a later vector var (0 at the start: it starts after)", lane("early") == 1.0f);
    Expect("a var swizzling itself is a vector", lane("grows", 0) == 1.0f && lane("grows", 1) == 2.0f &&
                                                     lane("grows", 2) == 3.0f);

    Names names;
    std::string error;
    Run("vec3(1, 2, 3) + vec2(1, 1)", names, nullptr, &error);
    Expect("vectors of different widths are refused", !error.empty());
    errors.clear();
    Expect("a swizzle past a scalar var is refused",
           Read(Parse(kNarrowSwizzle, "narrow.effect.toml"), "Narrow", &errors) == nullptr && errors.size() == 1 &&
               errors[0].find("reads past a value of 1 lane") != std::string::npos);
    errors.clear();
    Expect("a particle's vars hold at most eight floats",
           Read(Parse(kWideParticle, "wide.effect.toml"), "Wide", &errors) == nullptr && !errors.empty() &&
               errors[0].find("at most 8 floats") != std::string::npos);
}

// MF15l1's one field form: a tick alone also starts; the spawn as one point expression, drawn before the vars (which
// read it); a velocity's tick; an anchor's position as its start (by default its spawn's point) and its tick.
constexpr const char* kFieldForms = R"toml(
texture = "Stun"
[vars]
steps = { tick = "steps + 1" }
[[anchor]]
name = "orb"
count = 2
spawn = "vec3(a.index + 1, 0, 0)"
[[anchor]]
name = "ring"
count = 1
position = { start = "vec3(0, 0, 2)", tick = "a.position + vec3(1, 0, 0)" }
[[anchor]]
name = "fixed"
count = 1
position = { tick = "vec3(5, a.age, 0)" }
[[particle]]
name = "spark"
lifetime = 4
spawn = "ball() * size"
vars = { out = "normalize(p.spawn)", fade = { tick = "p.fade + 2" } }
velocity = { start = "p.out * 0.1", tick = "p.velocity * 0.9 + vec3(0, 0, -0.01)" }
size = { tick = "p.size * 0.9" }
alpha = { tick = "255 * (1 - p.t)" }
frame = { tick = "p.frame + 1", mode = "hold" }
)toml";

void CheckFieldForms() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kFieldForms, "forms.effect.toml"), "Forms", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the field forms record reads", false);
    const ParticleKind& kind = record->particles[0];
    Expect("a var's tick alone starts it", record->vars[0].start.text == "steps + 1" &&
                                               kind.vars[1].start.text == "p.fade + 2");
    Expect("the spawn is one point expression", kind.spawn.width == 3);
    Expect("the velocity's start and tick", kind.velocity.text == "p.out * 0.1" && !kind.velocityTick.Empty());
    Expect("size, alpha and frame ticks alone start them", kind.sizeStart.text == "p.size * 0.9" &&
                                                               kind.alphaStart.text == kind.alphaTick.text &&
                                                               kind.frameStart.text == "p.frame + 1" &&
                                                               kind.frameMode == FrameMode::Hold);

    State state(record);
    IsaacRandom random;
    auto at = [&](int anchor, int k) { return state.anchors[anchor].values[static_cast<int>(AnchorValue::PositionX) + k]; };
    state.Step(random);
    Expect("a source var's tick starts it", state.vars[0] == 1.0f);
    Expect("an anchor starts at its spawn's point", state.anchors.size() == 4 && at(0, 0) == 1.0f && at(1, 0) == 2.0f &&
                                                        state.anchors[1].values[static_cast<int>(AnchorValue::SpawnX)] == 2.0f);
    Expect("an anchor's position start", at(2, 0) == 0.0f && at(2, 2) == 2.0f);
    Expect("an anchor's position tick alone starts it", at(3, 0) == 5.0f && at(3, 1) == 0.0f);
    state.Step(random);
    Expect("then the var ticks", state.vars[0] == 2.0f);
    Expect("an anchor without a tick stays", at(0, 0) == 1.0f && at(1, 0) == 2.0f);
    Expect("an anchor's position ticks", at(2, 0) == 1.0f && at(2, 2) == 2.0f && at(3, 1) == 1.0f);
}

} // namespace

int RunParticleRecordChecks() {
    CheckExpressions();
    CheckRecord();
    CheckState();
    CheckReactions();
    CheckPlacement();
    CheckCarrierReaders();
    CheckExtensions();
    CheckReactionParticles();
    CheckCastFeatures();
    CheckBlastFeatures();
    CheckWrittenEffects();
    CheckLike();
    CheckVectors();
    CheckFieldForms();
    return failures;
}
