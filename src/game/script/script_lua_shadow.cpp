// The lockstep of a world script with its converted Lua in the running game. See script_lua_shadow.h and
// docs/notes/lua-scripting.md, "Lockstep in the game".
#include "game/script/script_lua_shadow.h"

#include "game/core/random.h"
#include "game/objects/map_object.h"
#include "game/script/script_addon.h"
#include "game/script/script_ir.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_scheduler.h"
#include "game/script/script_lua_writer.h"
#include "game/world/world.h"
#include "lib/bit_flags.h"
#include "platform/log.h"
#include "platform/platform.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scriptshadow {

using Objects = std::vector<const MapObject*>;

// A call's prepared arguments, each type in its slots' order.
struct Values {
    std::vector<float> floats;
    std::vector<std::string> strings;
    Objects objects;
    std::vector<Objects> groups;
};

// What the game's side put into the log: a built-in call, or an event the Lua side follows where it stands in the
// log (a text loaded, the zone reset, an object removed, a loop's condition evaluated beforehand that drew).
enum class Kind : std::uint8_t { Call, Load, ZoneReset, Purge, Drew };

struct Entry {
    Kind kind = Kind::Call;
    ScriptFunction id{};
    char type = 0;       // a function's result letter ('F', 'S', 'O', 'G'); 0 for a command
    bool inner = false;  // made inside a command's handler or a loop condition's evaluation beforehand: not compared
    int depth = 0;       // the loops (For, ForIf, Every, Any, Sum) running around it
    Values in;
    float number = 0.0f;
    std::string text;
    ScriptObjectRef object;
    ScriptGroup group;
    bool goOn = true;
    // A command that changed its group arguments: each of them after it.
    std::vector<ScriptGroup> groupsAfter;
    bool objectChanged = false;
    std::string definition;   // CallScript's, by its qualified name
    // Load: the converted text, its chunk name, whether its WorldScript starts.
    std::string lua;
    std::string chunk;
    bool run = false;
    const MapObject* removed = nullptr;   // Purge
    std::size_t logged = 0;   // while its handler runs: the log's length when it began
};

Objects Members(const ScriptGroup& group) {
    Objects members;
    members.reserve(group.size());
    for (const ScriptObjectRef& member : group) members.push_back(member.object);
    return members;
}

// The call's arguments; a loop's group only (its counter and its condition are the Lua's own).
Values Capture(const ScriptCall* call, bool loop) {
    Values values;
    if (loop) {
        if (!call->groupArgs.empty()) values.groups.push_back(Members(*call->groupArgs[0]));
        return values;
    }
    values.floats = call->floatArgs;
    values.strings = call->stringArgs;
    for (const ScriptObjectRef* object : call->objectArgs) values.objects.push_back(object ? static_cast<const MapObject*>(object->object) : nullptr);
    for (const ScriptGroup* group : call->groupArgs) values.groups.push_back(group ? Members(*group) : Objects{});
    return values;
}

bool SameFloats(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) return false;
    return true;
}

bool Same(const Values& a, const Values& b) {
    return SameFloats(a.floats, b.floats) && a.strings == b.strings && a.objects == b.objects && a.groups == b.groups;
}

std::string FloatText(float value) {
    if (value != value) return std::format("nan:{:08X}", std::bit_cast<std::uint32_t>(value));
    return std::format("{}", value);
}

std::string ObjectText(const MapObject* object) { return object ? std::format("#{}", object->id) : "none"; }

std::string GroupText(const Objects& members) {
    std::string text = "{";
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (i == 8) {
            text += std::format(" ... {} in all", members.size());
            break;
        }
        text += " " + ObjectText(members[i]);
    }
    return text + " }";
}

std::string ValuesText(const Values& values) {
    std::string text;
    const auto add = [&text](const std::string& value) { text += (text.empty() ? "" : ", ") + value; };
    for (float value : values.floats) add(FloatText(value));
    for (const std::string& value : values.strings) add("\"" + value + "\"");
    for (const MapObject* value : values.objects) add(ObjectText(value));
    for (const Objects& value : values.groups) add(GroupText(value));
    return text;
}

std::string CallName(ScriptFunction id) {
    if (id == ScriptFunction::CallScript) return "a script call";
    const char* keyword = KeywordName(static_cast<ScriptLexeme>(id));
    return keyword ? keyword : std::format("keyword {}", static_cast<int>(id));
}

std::string CallText(ScriptFunction id, const Values& values, int depth, const std::string& definition = {}) {
    std::string text = id == ScriptFunction::CallScript ? definition : CallName(id);
    text += "(" + ValuesText(values) + ")";
    if (depth) text += std::format(" in {} loop{}", depth, depth == 1 ? "" : "s");
    return text;
}

// The first difference of any shadow, and every live shadow (for the summary). Never freed: the world's scripts
// release their shadows from their destructors, after the other statics may be gone.
std::string& FirstFailure() {
    static auto* failure = new std::string;
    return *failure;
}

std::vector<Shadow*>& Shadows() {
    static auto* shadows = new std::vector<Shadow*>;
    return *shadows;
}

// A global the tick's compare reads: by its type and slot in the game's globals, its Lua name.
struct Watched {
    ScriptType type{};
    std::size_t index = 0;
    std::string name;   // the tree's
    std::string lua;
};

struct Shadow {
    Shadow(ScriptShadow* outer, Script* real, std::string label) : real(real), label(std::move(label)) {
        own.shadow = outer;
        Shadows().push_back(this);
    }
    ~Shadow() {
        std::erase(Shadows(), this);
        scheduler.reset();
        own.shadow = nullptr;
    }

    // ---- The game's side: the log ----

    bool Recording() const { return active && recording; }

    // A handler opens: its frame holds the entry until it closes (the calls and events inside it go first: the log
    // is in the order the calls end, as the Lua side makes them).
    std::size_t Begin(const ScriptCall* call, char type) {
        const bool loop = ScriptShadow::IsLoop(call->id);
        Entry& entry = frames.emplace_back();
        entry.id = call->id;
        entry.type = type;
        entry.inner = commandFrames > 0 || preEvaluations > 0;
        entry.depth = loopFrames;
        entry.logged = queue.size();
        if (!entry.inner) entry.in = Capture(call, loop);
        if (loop) ++loopFrames;
        else ++commandFrames;
        return frames.size() - 1;
    }

    // The handler of frame `index` closed: its entry, or none when the shadow stopped meanwhile.
    bool Finish(std::size_t index, Entry* entry) {
        if (index + 1 != frames.size()) return false;
        *entry = std::move(frames.back());
        frames.pop_back();
        if (ScriptShadow::IsLoop(entry->id)) --loopFrames;
        else --commandFrames;
        return Recording() && !entry->inner;
    }

    void Push(Entry&& entry) { queue.push_back(std::move(entry)); }

    // An event of the real script: in its tick logged where it came, else followed at once (the Lua state is the
    // game's at the end of its last tick, so the order with the calls is kept).
    void Event(Entry&& entry) {
        queue.push_back(std::move(entry));
        if (recording) return;
        cursor = 0;
        Apply(queue.size() - 1, {}, 0);
        queue.clear();
    }

    // ---- The Lua's side: the replay ----

    // The next recorded call, the events before it kept for after the compare; null when the log has none.
    Entry* Take() {
        pending.clear();
        while (cursor < queue.size() && (queue[cursor].kind != Kind::Call || queue[cursor].inner))
            if (queue[cursor++].kind != Kind::Call) pending.push_back(cursor - 1);
        if (cursor == queue.size()) return nullptr;
        return &queue[cursor++];
    }

    std::string Where() {
        lua_State* L = scheduler ? scheduler->lua.calling : nullptr;
        if (!L) return {};
        luaL_where(L, 1);
        std::string where = lua_tostring(L, -1);
        lua_pop(L, 1);
        return where.empty() ? where : " (Lua " + where.substr(0, where.size() - 1) + ")";
    }

    // The recorded call the Lua's call (`id`, `type`, `values`) matches, its events applied; null after a difference.
    Entry* Match(ScriptFunction id, char type, const Values& values, const std::string& definition = {}) {
        if (!active) return nullptr;
        if (!replaying || applying) {
            Fail("Lua calls " + CallText(id, values, luaDepth, definition) + " outside its tick" + Where());
            return nullptr;
        }
        Entry* entry = Take();
        if (!entry) {
            Fail("Lua calls " + CallText(id, values, luaDepth, definition) + "; the game made no more calls" + Where());
            return nullptr;
        }
        if (entry->id != id || entry->type != type || entry->depth != luaDepth || !Same(entry->in, values) ||
            entry->definition != definition) {
            Fail("the game calls " + CallText(entry->id, entry->in, entry->depth, entry->definition) + ", Lua calls " +
                 CallText(id, values, luaDepth, definition) + Where());
            return nullptr;
        }
        ++calls;
        ApplyPending();
        return active ? entry : nullptr;
    }

    void ApplyPending() {
        const std::vector<std::size_t> events = std::move(pending);
        pending.clear();
        for (std::size_t i = 0; i < events.size() && active; ++i) Apply(events[i], events, i);
    }

    // Event `index` of the log (`events`[`at`] when it came before a call, else past the cursor).
    void Apply(std::size_t index, const std::vector<std::size_t>& events, std::size_t at) {
        Entry& entry = queue[index];
        applying = true;
        switch (entry.kind) {
        case Kind::Load: Load(entry); break;
        case Kind::ZoneReset:
            if (replaying) Fail("the zone reset came inside a tick");
            else if (scheduler) scheduler->ResetZone();
            break;
        case Kind::Purge: {
            if (!scheduler) break;
            // The objects the game had removed by then: removed, and not by a later event of this log.
            std::set<const MapObject*> later;
            for (std::size_t i = at + 1; i < events.size(); ++i)
                if (queue[events[i]].kind == Kind::Purge) later.insert(queue[events[i]].removed);
            for (std::size_t i = std::max(cursor, index + 1); i < queue.size(); ++i)
                if (queue[i].kind == Kind::Purge) later.insert(queue[i].removed);
            scheduler->lua.RemoveObjects([&later](const MapObject* object) {
                return lib::Has(object->flags, MapObjectFlag::Removed) && !later.contains(object);
            });
            break;
        }
        case Kind::Drew:
            Fail("the game evaluated a loop's condition before the loop (" + CallName(entry.id) +
                 ") and it drew from the random generator; the Lua makes no such call");
            break;
        case Kind::Call: break;
        }
        applying = false;
    }

    void Load(const Entry& entry) {
        std::string error;
        if (!Scheduler().Load(entry.lua, entry.chunk.c_str(), entry.run, &error))
            Fail("Lua error loading " + entry.chunk + ": " + error);
        NameDefinitions();
    }

    // The definitions' tables by their names (the instance compare's); the globals watched again.
    void NameDefinitions() {
        watchedDirty = true;
        definitionNames.clear();
        lua_State* L = scheduler->lua.State();
        for (const auto& [name, lua] : scheduler->names.scripts) {
            lua_getglobal(L, lua.c_str());
            if (lua_type(L, -1) == LUA_TTABLE) definitionNames[lua_topointer(L, -1)] = name;
            lua_pop(L, 1);
        }
    }

    // ---- Saves ----

    // The real script's state came from a save (Script::LoadState): the Lua's is built from it by the old-save shim,
    // and compared at once.
    void Resume() {
        if (recording) return Stop("a save was loaded inside the script's tick");
        queue.clear();
        Clear();
        scheduler = MakeScheduler();
        std::string text, error;
        const bool ok = scheduler->Resume(*real, &text, &error);
        texts.emplace_back(std::format("save{}", ++resumes), text);
        if (!ok) return Fail("the save's script state did not resume in Lua: " + error);
        NameDefinitions();
        Compare();
    }

    // The Lua state saved (Eris) and restored into a new scheduler, which goes on in its place; compared at once.
    void RoundTrip() {
        if (!active || recording || replaying || !scheduler) return;
        std::string data, error;
        if (!scheduler->Save(&data, &error)) return Fail("the Lua state did not save: " + error);
        std::unique_ptr<ScriptLuaScheduler> restored = MakeScheduler();
        if (!restored->Restore(data, &error)) return Fail("the Lua state did not restore: " + error);
        scheduler = std::move(restored);
        ++roundTrips;
        savedBytes = std::max(savedBytes, data.size());
        NameDefinitions();
        Compare();
    }

    // ---- The tick ----

    void Tick() {
        cursor = 0;
        pending.clear();
        replaying = true;
        Scheduler().Tick();
        replaying = false;
        // The game's calls the Lua did not make, and the events after the last one.
        while (active && cursor < queue.size()) {
            const Entry& entry = queue[cursor++];
            if (entry.kind != Kind::Call) Apply(cursor - 1, {}, 0);
            else if (!entry.inner)
                Fail("the game calls " + CallText(entry.id, entry.in, entry.depth, entry.definition) +
                     ", Lua makes no more calls");
        }
        if (active && !luaErrors.empty()) Fail("Lua error: " + luaErrors.front());
        luaErrors.clear();
        if (active) Compare();
        queue.clear();
        cursor = 0;
        ++ticks;
    }

    void Compare() {
        // The instances, by definition.
        const std::vector<const void*> definitions = scheduler->InstanceDefinitions();
        if (definitions.size() != real->instances.size())
            return Fail(std::format("instances: {} in the game, {} in Lua", real->instances.size(), definitions.size()));
        std::size_t i = 0;
        for (const ScriptDefinition& instance : real->instances) {
            const auto found = definitionNames.find(definitions[i]);
            const std::string lua = found == definitionNames.end() ? "an unknown table" : found->second;
            if (lua != instance.name)
                return Fail(std::format("instance {}: {} in the game, {} in Lua", i, instance.name, lua));
            ++i;
        }
        // The waiting runs, by their positions.
        const std::vector<ScriptRunPosition> positions = scheduler->RunPositions();
        if (positions.size() != real->runs.size())
            return Fail(std::format("waiting runs: {} in the game, {} in Lua", real->runs.size(), positions.size()));
        i = 0;
        for (const ScriptRun& run : real->runs) {
            const ScriptRunPosition& lua = positions[i];
            if (run.position.ticks != lua.ticks || run.position.sleepStart != lua.sleepStart)
                return Fail(std::format("waiting run {}: ticks {} and sleep start {} in the game, {} and {} in Lua", i,
                                        run.position.ticks, run.position.sleepStart, lua.ticks, lua.sleepStart));
            ++i;
        }
        CompareGlobals();
    }

    void Watch() {
        watched.clear();
        const scriptlua::LuaNames& names = scheduler->names;
        const ScriptVars& globals = real->globals;
        const std::size_t sizes[] = {globals.floats.values.size(), globals.strings.values.size(),
                                     globals.objects.values.size(), globals.groups.values.size()};
        for (int t = 0; t < 4; ++t) {
            const ScriptType type = static_cast<ScriptType>(t + 1);
            watchedSizes[t] = sizes[t];
            for (std::size_t index = 0; index < sizes[t]; ++index) {
                const std::string& name = scriptir::VariableName(globals, type, static_cast<int>(index));
                const auto found = names.globals.find(name);
                if (found == names.globals.end()) continue;   // a declaration every use finds an earlier one of
                watched.push_back({type, index, name, found->second});
            }
        }
        watchedDirty = false;
    }

    void CompareGlobals() {
        const ScriptVars& globals = real->globals;
        if (watchedDirty || watchedSizes[0] != globals.floats.values.size() ||
            watchedSizes[1] != globals.strings.values.size() || watchedSizes[2] != globals.objects.values.size() ||
            watchedSizes[3] != globals.groups.values.size())
            Watch();
        lua_State* L = scheduler->lua.State();
        for (const Watched& global : watched) {
            lua_getglobal(L, global.lua.c_str());
            std::string game, lua;
            switch (global.type) {
            case ScriptType::Float: {
                const float value = globals.floats.values[global.index];
                const bool number = lua_type(L, -1) == LUA_TNUMBER;
                const float other = number ? scriptlua::FromLuaNumber(lua_tonumber(L, -1)) : 0.0f;
                if (!number || std::bit_cast<std::uint32_t>(value) != std::bit_cast<std::uint32_t>(other)) {
                    game = FloatText(value);
                    lua = number ? FloatText(other) : std::string("not a number");
                }
                break;
            }
            case ScriptType::String: {
                const std::string& value = globals.strings.values[global.index];
                const char* other = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
                if (!other || value != other) {
                    game = "\"" + value + "\"";
                    lua = other ? "\"" + std::string(other) + "\"" : "not a string";
                }
                break;
            }
            case ScriptType::Object: {
                const MapObject* value = globals.objects.values[global.index].object;
                const bool object = lua_isnil(L, -1) || luaL_testudata(L, -1, "ei.object");
                const MapObject* other = object && !lua_isnil(L, -1) ? scriptlua::CheckObject(L, -1)->object : nullptr;
                if (!object || value != other) {
                    game = ObjectText(value);
                    lua = object ? ObjectText(other) : "not an object";
                }
                break;
            }
            case ScriptType::Group: {
                const Objects value = Members(globals.groups.values[global.index]);
                const bool group = luaL_testudata(L, -1, "ei.group") != nullptr;
                const Objects other = group ? Members(*scriptlua::CheckGroup(L, -1)) : Objects{};
                if (!group || value != other) {
                    game = GroupText(value);
                    lua = group ? GroupText(other) : "not a group";
                }
                break;
            }
            default: break;
            }
            lua_pop(L, 1);
            if (!game.empty() || !lua.empty()) return Fail(global.name + ": " + game + " in the game, " + lua + " in Lua");
        }
    }

    // ---- State ----

    // The script's reset: the Lua state goes, a new one comes with the next text (made then: the world's last reset
    // comes after the C++ runtime's teardown began).
    void Clear() {
        scheduler.reset();
        definitionNames.clear();
        watched.clear();
        watchedDirty = true;
        texts.clear();
    }

    ScriptLuaScheduler& Scheduler() {
        if (!scheduler) scheduler = MakeScheduler();
        return *scheduler;
    }

    // A scheduler on the shadow's own script whose starts are matched first and whose Lua errors are differences.
    std::unique_ptr<ScriptLuaScheduler> MakeScheduler() {
        auto made = std::make_unique<ScriptLuaScheduler>(&own);
        made->report = [this](const std::string& message) { luaErrors.push_back(message); };
        auto start = std::move(made->lua.startScript);
        made->lua.startScript = [this, start](lua_State* L) {
            Started(L);
            start(L);
        };
        return made;
    }

    // A call of a definition on the Lua side (the definition and the arguments on L's stack).
    void Started(lua_State* L) {
        Values values;
        for (int i = 2; i <= lua_gettop(L); ++i) {
            switch (lua_type(L, i)) {
            case LUA_TNUMBER: values.floats.push_back(scriptlua::FromLuaNumber(lua_tonumber(L, i))); break;
            case LUA_TSTRING: values.strings.emplace_back(lua_tostring(L, i)); break;
            case LUA_TNIL: values.objects.push_back(nullptr); break;
            default:
                if (luaL_testudata(L, i, "ei.object")) values.objects.push_back(scriptlua::CheckObject(L, i)->object);
                else if (luaL_testudata(L, i, "ei.group")) values.groups.push_back(Members(*scriptlua::CheckGroup(L, i)));
                break;
            }
        }
        const auto found = definitionNames.find(lua_topointer(L, 1));
        scheduler->lua.calling = L;
        Match(ScriptFunction::CallScript, 0, values, found == definitionNames.end() ? "an unknown table" : found->second);
    }

    void Fail(const std::string& what) {
        if (!active) return;
        active = false;
        failure = std::format("lua both: {} script, its tick {} (world tick {}): {}", label, ticks,
                              g_serverWorld.tick, what);
        ei::Log("%s", failure);
        if (FirstFailure().empty()) FirstFailure() = failure;
        // The converted texts beside the run's log, to read the lines named.
        for (const auto& [chunk, text] : texts)
            platform::WriteWholeFile(ei::OutputPath(std::format("luaboth_{}_{}.lua", label, chunk).c_str()), text);
    }

    // The shadow ends (a stop or a difference): the Lua state goes once nothing runs on it.
    void Stop(const std::string& why) {
        if (!active) return;
        active = false;
        stopped = why;
        ei::Log("lua both: %s script stopped at its tick %d: %s", label.c_str(), ticks, why.c_str());
    }

    void Release() {
        if (active || replaying || recording || !scheduler) return;
        frames.clear();
        queue.clear();
        pending.clear();
        scheduler.reset();
    }

    Script* real = nullptr;
    std::string label;
    Script own;   // the bindings' script (its temporaries); its effects stay empty: no command runs on it
    std::unique_ptr<ScriptLuaScheduler> scheduler;
    std::vector<std::string> luaErrors;   // the scheduler's reports of this tick
    std::map<const void*, std::string> definitionNames;   // a definition's table -> the tree's name
    std::vector<std::pair<std::string, std::string>> texts;   // chunk name, converted text (dumped on a difference)
    std::vector<Watched> watched;
    std::size_t watchedSizes[4] = {};
    bool watchedDirty = true;

    std::vector<Entry> queue;   // the log of the real script's tick
    std::size_t cursor = 0;
    std::vector<std::size_t> pending;
    std::vector<Entry> frames;   // the game's open handlers
    int loopFrames = 0;
    int commandFrames = 0;
    int preEvaluations = 0;
    bool recording = false;   // the real script's tick runs
    bool replaying = false;   // the scheduler's tick runs
    bool applying = false;
    int luaDepth = 0;
    std::vector<Objects> luaLoops;   // the Lua's running loops' groups

    bool active = true;
    std::string failure;
    std::string stopped;
    int ticks = 0;
    long long calls = 0;
    int textCount = 0;
    int cachedTexts = 0;           // of them, converted from the scheduler's cache
    int resumes = 0;               // saves loaded and resumed
    int roundTrips = 0;            // Eris round trips
    std::size_t savedBytes = 0;    // the largest persisted state
    // The shadow's tick of the next Eris round trip: 200, then twice as far each time (a few per run however long;
    // and one at every save of the game).
    int nextRoundTrip = 200;
    bool onLua = false;   // the real script runs on Lua (RunsOnLua): inactive until its next reset or old load
    int luaRuns = 0;      // the times it went on Lua
};

}   // namespace scriptshadow

using namespace scriptshadow;

ScriptShadow::~ScriptShadow() = default;

bool ScriptShadow::Enabled() {
    static const bool on = [] {
        const std::string value = platform::EnvironmentVariable("EI_LUA_BOTH");
        return !value.empty() && value != "0";
    }();
    return on;
}

void ScriptShadow::Attach(Script* script, const char* label) {
    if (script->shadow) return;
    auto* shadow = new ScriptShadow;
    shadow->state = std::make_unique<Shadow>(shadow, script, label);
    script->shadow = shadow;
}

void ScriptShadow::Release(Script* script) {
    ScriptShadow* shadow = script->shadow;
    if (!shadow || !shadow->state || shadow->state->real != script) return;
    script->shadow = nullptr;
    delete shadow;
}

std::string ScriptShadow::Failure() { return FirstFailure(); }

std::string ScriptShadow::Summary() {
    std::string text;
    for (const Shadow* shadow : Shadows()) {
        text += std::format("{}lua both: {} script: {} ticks, {} calls compared, {} texts ({} from the cache)",
                            text.empty() ? "" : "\n", shadow->label, shadow->ticks, shadow->calls, shadow->textCount,
                            shadow->cachedTexts);
        if (shadow->resumes || shadow->roundTrips)
            text += std::format("; {} saves resumed, {} Eris round trips (at most {} bytes)", shadow->resumes,
                                shadow->roundTrips, shadow->savedBytes);
        if (shadow->luaRuns) text += std::format("; {} times on a mod's Lua (not compared)", shadow->luaRuns);
        if (!shadow->failure.empty()) text += "; differs";
        else if (!shadow->stopped.empty()) text += "; stopped: " + shadow->stopped;
    }
    return text;
}

bool ScriptShadow::IsLoop(ScriptFunction id) {
    return id == ScriptFunction::For || id == ScriptFunction::ForIf || id == ScriptFunction::Every ||
           id == ScriptFunction::Any || id == ScriptFunction::Sum;
}

ScriptShadow::PreEvaluation::PreEvaluation(Script* script, ScriptFunction loop)
    : script(script), loop(loop), count(g_random.count), cc(g_random.cc) {
    ++script->shadow->state->preEvaluations;
}

ScriptShadow::PreEvaluation::~PreEvaluation() {
    Shadow& shadow = *script->shadow->state;
    --shadow.preEvaluations;
    if (script == shadow.real && shadow.Recording() && (g_random.count != count || g_random.cc != cc)) {
        Entry entry;
        entry.kind = Kind::Drew;
        entry.id = loop;
        shadow.Event(std::move(entry));
    }
}

// ---- The evaluation's hooks ------------------------------------------------------------------------------------

float ScriptShadow::Float(ScriptCall* call, Script* script, ScriptVars* vars) {
    Shadow& shadow = *state;
    if (script == shadow.real) {
        if (!shadow.Recording()) return call->EvaluateFloat(script, vars);
        const std::size_t frame = shadow.Begin(call, 'F');
        const float result = call->EvaluateFloat(script, vars);
        Entry entry;
        if (shadow.Finish(frame, &entry)) {
            entry.number = result;
            shadow.Push(std::move(entry));
        }
        return result;
    }
    const Entry* entry = shadow.Match(call->id, 'F', Capture(call, false));
    return entry ? entry->number : 0.0f;
}

void ScriptShadow::Object(ScriptCall* call, Script* script, ScriptObjectRef** result) {
    Shadow& shadow = *state;
    if (script == shadow.real) {
        if (!shadow.Recording()) return call->EvaluateObject(script, result);
        const std::size_t frame = shadow.Begin(call, 'O');
        call->EvaluateObject(script, result);
        Entry entry;
        if (shadow.Finish(frame, &entry)) {
            entry.object = **result;
            shadow.Push(std::move(entry));
        }
        return;
    }
    const Entry* entry = shadow.Match(call->id, 'O', Capture(call, false));
    *result = script->NewTempObject();
    if (entry) **result = entry->object;
}

void ScriptShadow::Group(ScriptCall* call, Script* script, ScriptGroup** result) {
    Shadow& shadow = *state;
    if (script == shadow.real) {
        if (!shadow.Recording()) return call->EvaluateGroup(script, result);
        const std::size_t frame = shadow.Begin(call, 'G');
        call->EvaluateGroup(script, result);
        Entry entry;
        if (shadow.Finish(frame, &entry)) {
            entry.group = **result;
            shadow.Push(std::move(entry));
        }
        return;
    }
    const Entry* entry = shadow.Match(call->id, 'G', Capture(call, false));
    *result = &script->tempGroups.emplace_back();
    if (entry) **result = entry->group;
}

void ScriptShadow::String(ScriptCall* call, Script* script, std::string* result) {
    Shadow& shadow = *state;
    if (script == shadow.real) {
        if (!shadow.Recording()) return scriptaddon::EvaluateString(call, script, result);
        const std::size_t frame = shadow.Begin(call, 'S');
        scriptaddon::EvaluateString(call, script, result);
        Entry entry;
        if (shadow.Finish(frame, &entry)) {
            entry.text = *result;
            shadow.Push(std::move(entry));
        }
        return;
    }
    const Entry* entry = shadow.Match(call->id, 'S', Capture(call, false));
    *result = entry ? entry->text : std::string();
}

bool ScriptShadow::Command(ScriptRun* run, ScriptCall* call, Script* script) {
    Shadow& shadow = *state;
    if (script == shadow.real) {
        // An assignment is the Lua's own `global = value`: its value's calls are compared, the globals after the tick.
        if (!shadow.Recording() || call->id == ScriptFunction::Assign) return run->Perform(script, call);
        const std::size_t frame = shadow.Begin(call, 0);
        if (call->id == ScriptFunction::CallScript)
            shadow.frames[frame].definition = script->definitions[static_cast<std::size_t>(call->target)].name;
        const bool goOn = run->Perform(script, call);
        Entry entry;
        if (shadow.Finish(frame, &entry)) {
            entry.goOn = goOn;
            if (!IsLoop(call->id) && call->id != ScriptFunction::CallScript) {
                bool groupChanged = false;
                for (std::size_t i = 0; i < call->groupArgs.size(); ++i)
                    groupChanged = groupChanged || Members(*call->groupArgs[i]) != entry.in.groups[i];
                if (groupChanged)
                    for (const ScriptGroup* group : call->groupArgs) entry.groupsAfter.push_back(*group);
                // An object the command removed from the world is dropped by the purge, which the Lua follows (by
                // the pointer only: the object may be gone).
                for (std::size_t i = 0; i < call->objectArgs.size(); ++i) {
                    const MapObject* before = entry.in.objects[i];
                    const MapObject* after = call->objectArgs[i]->object;
                    bool purged = false;
                    for (std::size_t k = entry.logged; k < shadow.queue.size() && !after && !purged; ++k)
                        purged = shadow.queue[k].kind == Kind::Purge && shadow.queue[k].removed == before;
                    entry.objectChanged = entry.objectChanged || (after != before && !purged);
                }
            }
            shadow.Push(std::move(entry));
        }
        return goOn;
    }
    const Entry* entry = shadow.Match(call->id, 0, Capture(call, false));
    if (!entry) return true;
    switch (call->id) {
    // The run-local waits run on the Lua's run: their go-on flags show its position.
    case ScriptFunction::Sleep:
    case ScriptFunction::WaitSegment:
    case ScriptFunction::SleepUntil: {
        const bool goOn = run->Perform(script, call);
        if (goOn != entry->goOn)
            shadow.Fail(std::format("{} {} in the game and {} in Lua (the run's ticks {}, sleep start {})",
                                    CallText(call->id, entry->in, entry->depth), entry->goOn ? "goes on" : "waits",
                                    goOn ? "goes on" : "waits", run->position.ticks, run->position.sleepStart));
        return goOn;
    }
    case ScriptFunction::KillScript:
        if (run->killFlag) *run->killFlag = true;   // the WorldScript's warns in the game, and kills nothing
        return true;
    default:
        if (entry->objectChanged)
            shadow.Fail(CallText(call->id, entry->in, entry->depth) +
                        " changed an object argument: the replay cannot follow");
        for (std::size_t i = 0; i < entry->groupsAfter.size() && i < call->groupArgs.size(); ++i)
            *call->groupArgs[i] = entry->groupsAfter[i];
        return entry->goOn;
    }
}

void ScriptShadow::EnterLoop(const ScriptGroup& group) {
    Shadow& shadow = *state;
    shadow.luaLoops.push_back(Members(group));
    ++shadow.luaDepth;
}

void ScriptShadow::LeaveLoop(ScriptFunction id, float result) {
    Shadow& shadow = *state;
    if (shadow.luaLoops.empty()) return;
    Values values;
    values.groups.push_back(std::move(shadow.luaLoops.back()));
    shadow.luaLoops.pop_back();
    --shadow.luaDepth;
    const char type = id == ScriptFunction::For || id == ScriptFunction::ForIf ? 0 : 'F';
    const Entry* entry = shadow.Match(id, type, values);
    if (entry && type == 'F' && std::bit_cast<std::uint32_t>(entry->number) != std::bit_cast<std::uint32_t>(result))
        shadow.Fail(std::format("{} gives {} in the game, {} in Lua{}", CallText(id, values, shadow.luaDepth),
                                FloatText(entry->number), FloatText(result), shadow.Where()));
}

// ---- The real script's events ----------------------------------------------------------------------------------

void ScriptShadow::Loaded(Script* script, const scriptir::File& file, std::string_view text, bool runWorldScript) {
    Shadow& shadow = *state;
    if (script != shadow.real || !shadow.active) return;
    // Converted at once, in the names of the texts before it (the scheduler's runtime conversion), run where the
    // game's tick had the load.
    ScriptLuaScheduler::Converted converted = shadow.Scheduler().Convert(file, text);
    ++shadow.textCount;
    if (converted.cached) ++shadow.cachedTexts;
    Entry entry;
    entry.kind = Kind::Load;
    entry.lua = std::move(converted.lua);
    entry.chunk = "=" + converted.chunk;
    entry.run = runWorldScript && file.ok;
    shadow.texts.emplace_back(converted.chunk, entry.lua);
    shadow.Event(std::move(entry));
}

void ScriptShadow::Reset(Script* script) {
    Shadow& shadow = *state;
    if (script == shadow.real && shadow.onLua) {
        shadow.onLua = false;
        shadow.active = true;
    }
    if (script != shadow.real || !shadow.active) return;
    if (shadow.recording) return shadow.Stop("the script was reset inside its tick");
    shadow.queue.clear();
    shadow.Clear();
}

void ScriptShadow::ZoneReset(Script* script) {
    Shadow& shadow = *state;
    if (script != shadow.real || !shadow.active) return;
    Entry entry;
    entry.kind = Kind::ZoneReset;
    shadow.Event(std::move(entry));
}

void ScriptShadow::Purged(Script* script, const MapObject* removed) {
    Shadow& shadow = *state;
    if (script != shadow.real || !shadow.active) return;
    Entry entry;
    entry.kind = Kind::Purge;
    entry.removed = removed;
    shadow.Event(std::move(entry));
}

void ScriptShadow::BeginTick(Script* script) {
    Shadow& shadow = *state;
    if (script != shadow.real || !shadow.active) return;
    if (shadow.recording) return shadow.Stop("the script ticked inside its tick");
    shadow.recording = true;
}

void ScriptShadow::EndTick(Script* script) {
    Shadow& shadow = *state;
    if (script != shadow.real) return;
    const bool ticked = shadow.recording;
    shadow.recording = false;
    if (shadow.active && ticked) {
        if (!shadow.frames.empty()) shadow.Fail("a handler of the game's was still open at the end of the tick");
        else shadow.Tick();
        if (shadow.ticks >= shadow.nextRoundTrip) {
            shadow.nextRoundTrip *= 2;
            shadow.RoundTrip();
        }
    }
    shadow.Release();
}

void ScriptShadow::StateLoaded(Script* script) {
    Shadow& shadow = *state;
    if (script == shadow.real && shadow.onLua) {
        shadow.onLua = false;
        shadow.active = true;
    }
    if (script != shadow.real || !shadow.active) return;
    shadow.Resume();
}

void ScriptShadow::Saving(Script* script) {
    Shadow& shadow = *state;
    if (script != shadow.real) return;
    shadow.RoundTrip();
}

void ScriptShadow::RunsOnLua(Script* script) {
    Shadow& shadow = *state;
    if (script != shadow.real || !shadow.active) return;
    if (shadow.recording) return shadow.Stop("the script went on Lua inside its tick");
    shadow.queue.clear();
    shadow.Clear();
    shadow.active = false;
    shadow.onLua = true;
    ++shadow.luaRuns;
    ei::Log("lua both: %s script runs on a mod's Lua at its tick %d: not compared until its next reset",
            shadow.label.c_str(), shadow.ticks);
}
