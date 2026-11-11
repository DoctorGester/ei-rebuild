// The data effects' records without the game (src/game/effects/particle_expr.h, particle_record.h): expressions
// compiled and run, a record read from TOML and merged over a lower layer's, its problems reported, the state machine
// driven by its typed inputs: stages by ticks, by a condition on an input and by an event, enter and event actions,
// anchors, conditional tick reactions and the stop action; and every whole effect the notes and the modding guide
// write out read without a problem. ei_unit_check (unit_check.cpp) runs it.
#include "../../src/game/effects/particle_record.h"
#include "../../src/game/core/random.h"
#include "../../src/lib/toml.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
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
radius = 2
angle = "a.index * pi / 2"
z = "aim.z"

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
               std::fabs(state.anchors[1].values[static_cast<int>(AnchorValue::Y)] - 2.0f) < 1e-6f &&
               state.anchors[2].values[static_cast<int>(AnchorValue::Z)] == 3.0f);
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
[[particle]]
name = "star"
lifetime = 3
size = "emitting ? carrier_radius : 0"
)toml";

void CheckReactions() {
    std::vector<std::string> errors;
    const std::shared_ptr<const Record> record = Read(Parse(kTicking, "ticking.effect.toml"), "Ticking", &errors);
    for (const std::string& error : errors) std::printf("ei_unit_check: particle record: unexpected: %s\n", error.c_str());
    if (!record) return Expect("the ticking record reads", false);
    Expect("keep_alive and a read of the carrier noted", record->keepAlive && record->readsCarrier);
    const std::shared_ptr<const Record> original = Read(Parse(kTicking, "ticking.effect.toml"), "Ticking", &errors, 0x2041);
    Expect("an original type's id given", original && original->id == 0x2041);
    State state(record);
    IsaacRandom random;
    state.Step(random);
    Expect("a tick reaction waits for its condition", !state.stopRequested);
    state.SetValue("armed", 1.0f);
    state.Step(random);
    Expect("its condition reads the source's values (no particles yet)", !state.stopRequested);
    state.source[static_cast<int>(SourceValue::Particles)] = 2.0f;
    state.Step(random);
    Expect("the condition holds: the stop asked for, the effect goes on",
           state.stopRequested && !state.ended && state.emitting);
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
    Expect("the notes' and the guide's effects found", read >= 2);
}

} // namespace

int RunParticleRecordChecks() {
    CheckExpressions();
    CheckRecord();
    CheckState();
    CheckReactions();
    CheckWrittenEffects();
    return failures;
}
