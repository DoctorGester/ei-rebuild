// The effects' Lua hooks: the client's own Lua state, the scripts by file, the two values the hooks get (`e`, `p`) and
// the particle budget. See particle_lua.h and docs/notes/effects.md, "Lua hooks".
#include "engine/particles/particle_lua.h"

#include "engine/core/lua_sandbox.h"
#include "engine/core/random.h"
#include "engine/particles/particle_host.h"
#include "engine/particles/particle_record.h"
#include "engine/particles/particle_source.h"

#include "lib/crt.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "platform/log.h"
#include "platform/platform.h"

#include "eris/lua.h"
#include "eris/lauxlib.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace effectlua {

// The hooks by their rows' order (particle_lua_api.inc).
enum class Hook : std::uint8_t {
#define EI_EFFECT_LUA_HOOK(name, params, returns, text) name,
#include "engine/particles/particle_lua_api.inc"
    Count
};
constexpr int kHooks = static_cast<int>(Hook::Count);
constexpr const char* kHookNames[] = {
#define EI_EFFECT_LUA_HOOK(name, params, returns, text) #name,
#include "engine/particles/particle_lua_api.inc"
};

struct Script {
    std::string file;       // as the record names it
    std::string path;       // where it was read, for messages
    int hooks[kHooks] = {}; // the registry's references to its functions (LUA_NOREF: none)
    bool broken = false;    // it did not load, or a hook failed: its hooks are off
    bool budgetTold = false;
};

namespace {

constexpr std::string_view kFolder = "Res\\effects";
constexpr const char* kSourceMeta = "ei.effect_source";
constexpr const char* kParticleMeta = "ei.particle";
// The VM instructions one hook (or the file's top level) may run: counted per kStepChunk by the count hook, so a
// loop that never ends is an error, not a frozen game.
constexpr int kStepChunk = 1000;
constexpr int kStepChunks = 1000;
// The state's memory: past it an allocation fails (a Lua error in the hook that made it).
constexpr std::size_t kMemoryLimit = 64u << 20;

// What the running hook works on.
struct Call {
    ParticleSource* source = nullptr;
    Script* script = nullptr;
    const void* table = nullptr;    // the source's `e`, as lua_topointer gives it
    Particle* particle = nullptr;
    const effectdata::ParticleKind* kind = nullptr;
    Vector3 spaceOrigin;
    bool killed = false;
};

std::string Lower(std::string_view text) {
    std::string lower(text);
    text::LowerInPlace(lower.data(), lower.size());
    return lower;
}

class Runtime {
public:
    Runtime();
    ~Runtime() { lua_close(L); }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // The script `file` (loaded at its first use; `effect` names the effect for its messages); null when it does not
    // load.
    Script* Find(const std::string& file, const std::string& effect);

    lua_State* L = nullptr;
    std::map<std::string, Script, std::less<>> scripts;   // by lower-case file
    int particle = LUA_NOREF;   // the one `p`, a userdata whose methods read Call::particle
    Call call;
    int chunks = 0;             // the count hook's calls in the running hook
    std::size_t memory = 0;

private:
    bool Load(Script& script, const std::string& text);
};

Runtime* g_runtime = nullptr;
// Deletes the state at the program's end (the sources' Instances then find none).
struct RuntimeOwner {
    ~RuntimeOwner() {
        delete g_runtime;
        g_runtime = nullptr;
    }
} g_owner;
std::uint32_t g_generation = 1;
int g_budget = kParticleBudget;
std::map<std::string, std::string, std::less<>> g_putScripts;   // PutScript's, by lower-case file

Runtime& Get() {
    if (!g_runtime) g_runtime = new Runtime();
    return *g_runtime;
}

void* Allocate(void* user, void* block, std::size_t old, std::size_t size) {
    Runtime& runtime = *static_cast<Runtime*>(user);
    if (!block) old = 0;
    if (size == 0) {
        runtime.memory -= old;
        std::free(block);
        return nullptr;
    }
    if (size > old && runtime.memory - old + size > kMemoryLimit) return nullptr;
    void* grown = std::realloc(block, size);
    if (grown) runtime.memory = runtime.memory - old + size;
    return grown;
}

// The count hook: past the limit an error where the hook's function is ("<file>:<line>:").
void CountSteps(lua_State* L, lua_Debug*) {
    if (++g_runtime->chunks <= kStepChunks) return;
    luaL_where(L, 0);
    lua_pushfstring(L, "ran over %d instructions", kStepChunk * kStepChunks);
    lua_concat(L, 2);
    lua_error(L);
}

// print(...): the values as tostring gives them, a tab apart, as one line of the log.
int PrintToLog(lua_State* L) {
    std::string line;
    for (int i = 1, count = lua_gettop(L); i <= count; ++i) {
        std::size_t size = 0;
        const char* text = luaL_tolstring(L, i, &size);
        if (i > 1) line += '\t';
        line.append(text, size);
        lua_pop(L, 1);
    }
    ei::Log("effects: lua: %s", line);
    return 0;
}

float Number(lua_State* L, int index) { return static_cast<float>(luaL_checknumber(L, index)); }

Vector3 Point(lua_State* L, int index) { return {Number(L, index), Number(L, index + 1), Number(L, index + 2)}; }

int PushNumbers(lua_State* L, const float* values, int count) {
    for (int i = 0; i < count; ++i) lua_pushnumber(L, values[i]);
    return count;
}

int Truncated(lua_Number value, int low, int high) {
    if (!(value > low)) return low;
    if (value >= high) return high;
    return static_cast<int>(value);
}

// ---- e: the source's methods -----------------------------------------------------------------------------------------

effectdata::State& SourceState(lua_State* L) {
    const Call& call = g_runtime->call;
    if (!call.source || lua_topointer(L, 1) != call.table)
        luaL_error(L, "an effect's source works only in its own hook (call its methods with ':')");
    return *call.source->data;
}

// The point's (on the stack's top) lane `k`: its [k + 1], else its field x, y or z.
float Lane(lua_State* L, int k) {
    if (lua_geti(L, -1, k + 1) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_getfield(L, -1, k == 0 ? "x" : k == 1 ? "y" : "z");
    }
    const float value = static_cast<float>(luaL_checknumber(L, -1));
    lua_pop(L, 1);
    return value;
}

// A list input's points: a table of {x, y, z} (or {x = , y = , z = }).
std::vector<Vector3> Points(lua_State* L, int index) {
    std::vector<Vector3> points;
    const lua_Integer count = luaL_len(L, index);
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_geti(L, index, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        points.push_back({Lane(L, 0), Lane(L, 1), Lane(L, 2)});
        lua_pop(L, 1);
    }
    return points;
}

int Source_set(lua_State* L) {
    effectdata::State& state = SourceState(L);
    const char* input = luaL_checkstring(L, 2);
    bool done = false;
    if (lua_gettop(L) >= 5) {
        const Vector3 value = Point(L, 3);
        done = state.SetVector(input, value.x, value.y, value.z);
    } else {
        switch (lua_type(L, 3)) {
        case LUA_TSTRING: done = state.SetEnum(input, lua_tostring(L, 3)); break;
        case LUA_TBOOLEAN: done = state.SetValue(input, lua_toboolean(L, 3) ? 1.0f : 0.0f); break;
        case LUA_TTABLE: done = state.SetList(input, Points(L, 3)); break;
        default: done = state.SetValue(input, Number(L, 3)); break;
        }
    }
    lua_pushboolean(L, done);
    return 1;
}

int Source_fire(lua_State* L) {
    effectdata::State& state = SourceState(L);
    lua_pushboolean(L, state.Fire(luaL_checkstring(L, 2)));
    return 1;
}

int Source_get(lua_State* L) {
    effectdata::State& state = SourceState(L);
    const std::string_view name = luaL_checkstring(L, 2);
    const effectdata::Record& record = *state.record;
    if (const effectdata::Input* input = record.FindInput(name)) {
        const float* value = state.inputs.data() + input->slot;
        switch (input->type) {
        case effectdata::InputType::Vector: return PushNumbers(L, value, 3);
        case effectdata::InputType::Enum: {
            const std::size_t index = static_cast<std::size_t>(*value);
            if (index < input->values.size()) lua_pushstring(L, input->values[index].c_str());
            else lua_pushnil(L);
            return 1;
        }
        default: return PushNumbers(L, value, 1);
        }
    }
    for (const effectdata::Var& var : record.vars)
        if (var.name == name) return PushNumbers(L, state.vars.data() + var.slot, var.width);
    if (name == "stage") {
        if (state.stage >= 0) lua_pushstring(L, record.stages[state.stage].name.c_str());
        else lua_pushnil(L);
        return 1;
    }
    int index = 0, width = 1;
    if (!effectdata::FindSourceValue(name, &index, &width)) return luaL_error(L, "the effect has no value %s", name.data());
    const int radius = static_cast<int>(effectdata::SourceValue::CarrierRadius);
    const int centre = static_cast<int>(effectdata::SourceValue::CarrierZ);
    if ((index == radius || index == centre) && state.measure && !state.measured) state.measure(state.measureContext);
    return PushNumbers(L, state.source.data() + index, width);
}

int Source_random(lua_State* L) {
    SourceState(L);
    const int count = lua_gettop(L);
    const float lo = count >= 3 ? Number(L, 2) : 0.0f;
    const float hi = count >= 3 ? Number(L, 3) : count == 2 ? Number(L, 2) : 1.0f;
    lua_pushnumber(L, g_particleRandom.Range(lo, hi));
    return 1;
}

int Source_pick(lua_State* L) {
    SourceState(L);
    const lua_Integer n = luaL_checkinteger(L, 2);
    luaL_argcheck(L, n >= 1 && n <= std::numeric_limits<std::uint32_t>::max(), 2, "a count of 1 or more");
    lua_pushinteger(L, g_particleRandom.Next() % static_cast<std::uint32_t>(n));
    return 1;
}

int Source_ground(lua_State* L) {
    SourceState(L);
    lua_pushnumber(L, particlehost::Current().GroundHeight(Number(L, 2), Number(L, 3)));
    return 1;
}

int Source_effect(lua_State* L) {
    effectdata::State& state = SourceState(L);
    effectdata::Started started;
    started.name = luaL_checkstring(L, 2);
    if (lua_gettop(L) >= 5) {
        started.placed = true;
        started.place = Point(L, 3);
    }
    state.effects.push_back(std::move(started));
    return 0;
}

// ---- p: the particle's methods ---------------------------------------------------------------------------------------

Particle& Current(lua_State* L) {
    Runtime& runtime = *g_runtime;
    if (!runtime.call.particle || !luaL_testudata(L, 1, kParticleMeta))
        luaL_error(L, "a particle works only in its own on_spawn or on_update (call its methods with ':')");
    return *runtime.call.particle;
}

const effectdata::Var* ParticleVar(std::string_view name) {
    for (const effectdata::Var& var : g_runtime->call.kind->vars)
        if (var.name == name) return &var;
    return nullptr;
}

int Particle_get(lua_State* L) {
    Particle& p = Current(L);
    const std::string_view name = luaL_checkstring(L, 2);
    if (name == "position") return PushNumbers(L, &p.pos.x, 3);
    if (name == "velocity") return PushNumbers(L, &p.velocity.x, 3);
    if (name == "size") return PushNumbers(L, &p.size, 1);
    if (name == "alpha" || name == "color") {
        const float channels[4] = {static_cast<float>(p.color >> 24), static_cast<float>((p.color >> 16) & 0xFF),
                                   static_cast<float>((p.color >> 8) & 0xFF), static_cast<float>(p.color & 0xFF)};
        return name == "alpha" ? PushNumbers(L, channels, 1) : PushNumbers(L, channels + 1, 3);
    }
    if (name == "frame") lua_pushinteger(L, p.frame);
    else if (name == "age") lua_pushinteger(L, p.age);
    else if (name == "life") lua_pushinteger(L, p.life);
    else if (name == "kind") lua_pushstring(L, g_runtime->call.kind->name.c_str());
    else if (const effectdata::Var* var = ParticleVar(name)) return PushNumbers(L, p.vars + var->slot, var->width);
    else return luaL_error(L, "a particle has no value %s", name.data());
    return 1;
}

std::uint32_t Channel(lua_State* L, int index) {
    return static_cast<std::uint32_t>(Truncated(luaL_checknumber(L, index), 0, 255));
}

int Particle_set(lua_State* L) {
    Particle& p = Current(L);
    const Call& call = g_runtime->call;
    const std::string_view name = luaL_checkstring(L, 2);
    if (name == "position") {
        p.pos = Point(L, 3);
        // Outside world space it rides its source or anchor by its offset.
        if (call.kind->space != effectdata::Space::World) p.drift = p.pos - call.spaceOrigin;
    } else if (name == "velocity") {
        p.velocity = Point(L, 3);
    } else if (name == "size") {
        p.size = Number(L, 3);
    } else if (name == "alpha") {
        p.color = Channel(L, 3) << 24 | (p.color & 0xFFFFFFu);
    } else if (name == "color") {
        p.color = (p.color & 0xFF000000u) | Channel(L, 3) << 16 | Channel(L, 4) << 8 | Channel(L, 5);
    } else if (name == "frame") {
        p.frame = Truncated(luaL_checknumber(L, 3), 0, std::numeric_limits<int>::max());
    } else if (name == "life") {
        p.life = Truncated(luaL_checknumber(L, 3), 0, std::numeric_limits<int>::max());
    } else if (const effectdata::Var* var = ParticleVar(name)) {
        for (int k = 0; k < var->width; ++k) p.vars[var->slot + k] = Number(L, 3 + k);
    } else {
        return luaL_error(L, "a particle has no value %s to set", name.data());
    }
    return 0;
}

int Particle_kill(lua_State* L) {
    Current(L);
    g_runtime->call.killed = true;
    return 0;
}

constexpr luaL_Reg kSourceMethods[] = {
#define EI_EFFECT_LUA_SOURCE_METHOD(name, params, returns, text) {#name, Source_##name},
#include "engine/particles/particle_lua_api.inc"
    {nullptr, nullptr},
};
constexpr luaL_Reg kParticleMethods[] = {
#define EI_EFFECT_LUA_PARTICLE_METHOD(name, params, returns, text) {#name, Particle_##name},
#include "engine/particles/particle_lua_api.inc"
    {nullptr, nullptr},
};

Runtime::Runtime() {
    L = lua_newstate(Allocate, this);
    if (!L) game::FatalExit("effects: no memory for the Lua state");
    // An error outside a protected call (none is expected: every hook and the files' top levels run under lua_pcall).
    lua_atpanic(L, [](lua_State* state) -> int { game::FatalExit("effects: Lua: %s", lua_tostring(state, -1)); });
    luasandbox::OpenLibraries(L);
    lua_register(L, "print", PrintToLog);
    luasandbox::ReplacePairsAndLoad(L);
    luaL_newmetatable(L, kSourceMeta);
    luaL_newlib(L, kSourceMethods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newmetatable(L, kParticleMeta);
    luaL_newlib(L, kParticleMethods);
    lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    lua_newuserdata(L, 1);
    luaL_setmetatable(L, kParticleMeta);
    particle = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_sethook(L, CountSteps, LUA_MASKCOUNT, kStepChunk);
}

Script* Runtime::Find(const std::string& file, const std::string& effect) {
    const std::string key = Lower(file);
    if (auto found = scripts.find(key); found != scripts.end()) return found->second.broken ? nullptr : &found->second;
    Script& script = scripts[key];
    script.file = file;
    for (int& hook : script.hooks) hook = LUA_NOREF;
    std::string text;
    bool read = false;
    if (auto put = g_putScripts.find(key); put != g_putScripts.end()) {
        text = put->second;
        script.path = std::string(kFolder) + "\\" + file;
        read = true;
    }
    // The top layer's file of the name.
    if (!read) {
        for (const platform::FolderLayer& layer : platform::FilesByLayer(kFolder, {file})) {
            for (const platform::LayerFile& found : layer.files)
                if (text::CompareIgnoringCase(found.name, file) == 0 && platform::ReadLayerFile(found.path, &text)) {
                    script.path = found.path;
                    read = true;
                    break;
                }
            if (read) break;
        }
    }
    if (!read) {
        game::Warning("effects: %s: no script %s\\%s; the effect runs without its hooks", effect, kFolder, file);
        script.broken = true;
        return nullptr;
    }
    if (!Load(script, text)) {
        script.broken = true;
        return nullptr;
    }
    ei::Log("effects: %s's hooks from %s", effect, script.path);
    return &script;
}

bool Runtime::Load(Script& script, const std::string& text) {
    const std::string chunk = "@" + script.path;
    if (luaL_loadbufferx(L, text.data(), text.size(), chunk.c_str(), "t") != LUA_OK) {
        game::Warning("effects: %s; the effect runs without its hooks", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // Its own globals over the shared ones: one script's names never reach another's.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setupvalue(L, -2, 1);
    chunks = 0;
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        game::Warning("effects: %s; the effect runs without its hooks", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (!lua_istable(L, -1)) {
        game::Warning("effects: %s gives no table of hooks; the effect runs without them", script.path);
        lua_pop(L, 1);
        return false;
    }
    for (int i = 0; i < kHooks; ++i) {
        const int type = lua_getfield(L, -1, kHookNames[i]);
        if (type == LUA_TFUNCTION) {
            script.hooks[i] = luaL_ref(L, LUA_REGISTRYINDEX);
            continue;
        }
        if (type != LUA_TNIL) game::Warning("effects: %s: %s is no function; left out", script.path, kHookNames[i]);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// The source's script while it can run `hook` (none: its source has no script, the state was reset, the script broke,
// or another hook runs).
Script* Ready(const ParticleSource& s, Hook hook) {
    const Instance* instance = s.script.get();
    if (!instance || !g_runtime || instance->generation != g_generation || g_runtime->call.source) return nullptr;
    Script* script = instance->script;
    return script->broken || script->hooks[static_cast<int>(hook)] == LUA_NOREF ? nullptr : script;
}

// Pushes the hook and `e` and makes the source the running call's.
void Begin(ParticleSource& s, Script& script, Hook hook) {
    Runtime& runtime = *g_runtime;
    lua_State* L = runtime.L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, script.hooks[static_cast<int>(hook)]);
    lua_rawgeti(L, LUA_REGISTRYINDEX, s.script->table);
    runtime.call.source = &s;
    runtime.call.script = &script;
    runtime.call.table = lua_topointer(L, -1);
}

// Calls the hook with `e` and `arguments` more; a failure turns the script's hooks off. Whether the hook killed its
// particle.
bool Finish(int arguments) {
    Runtime& runtime = *g_runtime;
    lua_State* L = runtime.L;
    runtime.chunks = 0;
    if (lua_pcall(L, 1 + arguments, 0, 0) != LUA_OK) {
        game::Warning("effects: %s; its hooks are off from now", lua_tostring(L, -1));
        lua_pop(L, 1);
        runtime.call.script->broken = true;
    }
    const bool killed = runtime.call.killed;
    runtime.call = {};
    return killed;
}

void Event(ParticleSource& s, const char* name, int count) {
    Script* script = Ready(s, Hook::on_event);
    if (!script) return;
    Begin(s, *script, Hook::on_event);
    lua_pushstring(g_runtime->L, name);
    lua_pushinteger(g_runtime->L, count);
    Finish(2);
}

// One particle call off the budget; false when it is spent (logged once per script).
bool Spend(Script& script) {
    if (g_budget > 0) {
        --g_budget;
        return true;
    }
    if (!script.budgetTold) {
        script.budgetTold = true;
        ei::Log("effects: %s: the particle budget (%d calls a tick) is spent; the tick's other particles go without "
                "their hooks",
                script.path, kParticleBudget);
    }
    return false;
}

bool ParticleHook(ParticleSource& s, Particle& p, Hook hook, const Vector3& spaceOrigin, int index) {
    Script* script = Ready(s, hook);
    if (!script || !Spend(*script)) return true;
    Runtime& runtime = *g_runtime;
    Begin(s, *script, hook);
    runtime.call.particle = &p;
    runtime.call.kind = &s.data->record->particles[static_cast<std::size_t>(p.kind)];
    runtime.call.spaceOrigin = spaceOrigin;
    lua_rawgeti(runtime.L, LUA_REGISTRYINDEX, runtime.particle);
    if (index >= 0) lua_pushinteger(runtime.L, index);
    return !Finish(index >= 0 ? 2 : 1);
}

} // namespace

Instance::~Instance() {
    if (g_runtime && generation == g_generation && table != LUA_NOREF) luaL_unref(g_runtime->L, LUA_REGISTRYINDEX, table);
}

std::unique_ptr<Instance> Attach(const effectdata::Record& record) {
    if (record.script.empty()) return nullptr;
    Runtime& runtime = Get();
    Script* script = runtime.Find(record.script, record.name);
    if (!script) return nullptr;
    auto instance = std::make_unique<Instance>();
    instance->script = script;
    instance->generation = g_generation;
    lua_newtable(runtime.L);
    luaL_setmetatable(runtime.L, kSourceMeta);
    instance->table = luaL_ref(runtime.L, LUA_REGISTRYINDEX);
    return instance;
}

void BeforeStep(ParticleSource& s, bool stopped) {
    Instance* instance = s.script.get();
    if (!instance || !g_runtime || instance->generation != g_generation) return;
    effectdata::State& state = *s.data;
    state.source[static_cast<int>(effectdata::SourceValue::Age)] = static_cast<float>(state.age);
    if (!instance->started) {
        instance->started = true;
        if (Script* script = Ready(s, Hook::on_start)) {
            Begin(s, *script, Hook::on_start);
            Finish(0);
        }
    }
    if (Script* script = Ready(s, Hook::on_tick)) {
        Begin(s, *script, Hook::on_tick);
        lua_pushinteger(g_runtime->L, state.age);
        Finish(1);
    }
    if (stopped) Event(s, "stop", 1);
    for (const effectdata::Input& input : state.record->inputs)
        if (input.type == effectdata::InputType::Event)
            if (const int fired = state.Pending(input.slot); fired > 0) Event(s, input.name.c_str(), fired);
}

void AfterStep(ParticleSource& s) {
    Instance* instance = s.script.get();
    if (!instance || instance->ended || !s.data->ended) return;
    instance->ended = true;
    Event(s, "end", 1);
}

void Spawned(ParticleSource& s) {
    if (s.script) Event(s, "spawn", 1);
}

bool OnSpawn(ParticleSource& s, Particle& p, int index, const Vector3& spaceOrigin) {
    return ParticleHook(s, p, Hook::on_spawn, spaceOrigin, index);
}

bool OnUpdate(ParticleSource& s, Particle& p, const Vector3& spaceOrigin) {
    return ParticleHook(s, p, Hook::on_update, spaceOrigin, -1);
}

bool HasSpawn(const ParticleSource& s) { return s.script && Ready(s, Hook::on_spawn); }

bool HasUpdate(const ParticleSource& s) { return s.script && Ready(s, Hook::on_update); }

void NewTick() { g_budget = kParticleBudget; }

int BudgetLeft() { return g_budget; }

void Reset() {
    delete g_runtime;
    g_runtime = nullptr;
    ++g_generation;
    g_putScripts.clear();
}

void PutScript(const std::string& file, std::string text) { g_putScripts[Lower(file)] = std::move(text); }

std::vector<std::string> MethodNames() {
    lua_State* L = Get().L;
    std::vector<std::string> names;
    for (const auto& [meta, kind] : {std::pair{kSourceMeta, "source:"}, std::pair{kParticleMeta, "particle:"}}) {
        luaL_getmetatable(L, meta);
        lua_getfield(L, -1, "__index");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1))
                names.push_back(kind + std::string(lua_tostring(L, -2)));
            lua_pop(L, 1);
        }
        lua_pop(L, 2);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace effectlua
