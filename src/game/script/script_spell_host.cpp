// The scripted spells' runtime in the zone's server Lua state. See script_spell_host.h and docs/notes/items_spells.md,
// "Scripted spells".
#include "game/script/script_spell_host.h"

#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_scheduler.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"

#include "lib/text_case.h"
#include "platform/platform.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace {

constexpr const char* kFolder = "Res\\spells";
// The ticks a cast lives at most when its spell has no duration.
constexpr int kDefaultTickCap = 600;
// The longest delay a timer takes, in ticks.
constexpr double kMaxDelay = 1e9;

bool ReadFromLayers(const std::string& file, std::string* text) {
    for (const platform::FolderLayer& layer : platform::FilesByLayer(kFolder, {file}))
        for (const platform::LayerFile& found : layer.files)
            if (platform::ReadLayerFile(found.path, text)) return true;
    return false;
}

const std::string& ScriptOf(const SpellEffect& effect) {
    static const std::string none;
    const itemdb::SpellRecord* record = effect.HeldSpell()->GetPrototype();
    return record ? record->script : none;
}

void SetNumber(lua_State* L, int table, const char* name, float value) {
    lua_pushnumber(L, scriptlua::ToLuaNumber(value));
    lua_setfield(L, table, name);
}

void SetInteger(lua_State* L, int table, const char* name, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setfield(L, table, name);
}

void PushPoint(lua_State* L, const Vector3& point) {
    lua_createtable(L, 0, 3);
    const int table = lua_gettop(L);
    SetNumber(L, table, "x", point.x);
    SetNumber(L, table, "y", point.y);
    SetNumber(L, table, "z", point.z);
}

lua_Integer IntegerField(lua_State* L, int table, const char* name) {
    lua_getfield(L, table, name);
    const lua_Integer value = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    return value;
}

// The entry of the cast value `self` (argument 1) of a method, from the host's casts table (its upvalue's registry
// reference) pushed; a Lua error when the cast has ended.
int CheckEntry(lua_State* L, int casts, const char* method) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_getfield(L, 1, "id");
    if (lua_isinteger(L, -1)) lua_rawget(L, -2);
    if (!lua_istable(L, -1)) return luaL_error(L, "%s: the cast has ended", method);
    lua_remove(L, -2);
    return lua_gettop(L);
}

} // namespace

ScriptSpellHost::ScriptSpellHost(ScriptLuaScheduler& owner)
    : readModule(ReadFromLayers), scheduler(owner), L(owner.lua.State()) {
    lua_newtable(L);
    casts = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    modules = luaL_ref(L, LUA_REGISTRYINDEX);
    // The cast values' metatable and its methods, permanents: a cast value a script keeps in its data persists.
    lua_createtable(L, 0, 1);
    lua_createtable(L, 0, 2);
    const int methods = lua_gettop(L);
    const auto method = [this, methods](const char* name, lua_CFunction function) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, function, 1);
        lua_pushvalue(L, -1);
        scheduler.lua.AddPermanent(std::format("ei.cast.{}", name).c_str());
        lua_setfield(L, methods, name);
    };
    method("After", After);
    method("Finish", Finish);
    lua_pushvalue(L, methods);
    scheduler.lua.AddPermanent("ei.cast.methods");
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, -1);
    scheduler.lua.AddPermanent("ei.cast");
    castMeta = luaL_ref(L, LUA_REGISTRYINDEX);
}

ScriptSpellHost::~ScriptSpellHost() {
    for (const auto& [key, effect] : live) effect->host = nullptr;
    luaL_unref(L, LUA_REGISTRYINDEX, casts);
    luaL_unref(L, LUA_REGISTRYINDEX, modules);
    luaL_unref(L, LUA_REGISTRYINDEX, castMeta);
}

bool ScriptSpellHost::Cast(SpellEffect& effect) {
    const int key = nextCast++;
    effect.castKey = key;
    effect.host = this;
    live[key] = &effect;
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_createtable(L, 0, 5);
    const int entry = lua_gettop(L);
    lua_newtable(L);
    lua_setfield(L, entry, "data");
    lua_newtable(L);
    lua_setfield(L, entry, "timers");
    SetInteger(L, entry, "tick", 0);
    SetInteger(L, entry, "seq", 0);
    lua_rawseti(L, -2, key);
    lua_pop(L, 1);

    PushModule(effect);
    const bool loaded = lua_istable(L, -1);
    lua_pop(L, 1);
    if (!loaded) {
        Drop(effect);
        return false;
    }
    return Settle(effect, Call(effect, "OnCast", 0));
}

bool ScriptSpellHost::Tick(SpellEffect& effect) {
    const int base = lua_gettop(L);
    PushEntry(L, effect.castKey);
    if (!lua_istable(L, -1)) {
        lua_settop(L, base);
        Drop(effect);
        return false;
    }
    const lua_Integer tick = IntegerField(L, base + 1, "tick") + 1;
    SetInteger(L, base + 1, "tick", tick);
    lua_settop(L, base);

    // The due timers one at a time by their due tick, then the order they were set in: a timer's function may set
    // more (none due before the next tick).
    for (;;) {
        PushEntry(L, effect.castKey);
        const int entry = base + 1;
        if (!lua_istable(L, entry)) {
            lua_settop(L, base);
            return false;
        }
        lua_getfield(L, entry, "timers");
        const int timers = base + 2;
        const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, timers));
        lua_Integer best = 0, bestDue = 0, bestSeq = 0;
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, timers, i);
            const lua_Integer due = IntegerField(L, -1, "due");
            const lua_Integer seq = IntegerField(L, -1, "seq");
            lua_pop(L, 1);
            if (due > tick || (best && (due > bestDue || (due == bestDue && seq > bestSeq)))) continue;
            best = i;
            bestDue = due;
            bestSeq = seq;
        }
        if (!best) {
            lua_settop(L, base);
            break;
        }
        lua_rawgeti(L, timers, best);
        lua_getfield(L, -1, "name");
        const std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        lua_getfield(L, -1, "payload");
        const int payload = lua_gettop(L);
        for (lua_Integer i = best; i < count; ++i) {
            lua_rawgeti(L, timers, i + 1);
            lua_rawseti(L, timers, i);
        }
        lua_pushnil(L);
        lua_rawseti(L, timers, count);
        lua_copy(L, payload, base + 1);
        lua_settop(L, base + 1);
        Hook result = Call(effect, name.c_str(), 1);
        if (result == Hook::Absent) {
            Report(std::format("{}: no function {} for a timer", ScriptOf(effect), name));
            result = Hook::Failed;
        }
        if (result == Hook::Failed) return Settle(effect, result);
        PushEntry(L, effect.castKey);
        const bool finished = EntryFlag(lua_gettop(L), "finished");
        lua_settop(L, base);
        if (finished) return Settle(effect, result);
    }
    return Settle(effect, Call(effect, "OnTick", 0));
}

void ScriptSpellHost::Forget(SpellEffect& effect) { Drop(effect); }

bool ScriptSpellHost::Adopt(SpellEffect& effect) {
    PushEntry(L, effect.castKey);
    const bool found = lua_istable(L, -1);
    lua_pop(L, 1);
    if (!found) return false;
    effect.host = this;
    live[effect.castKey] = &effect;
    nextCast = std::max(nextCast, effect.castKey + 1);
    return true;
}

void ScriptSpellHost::Save(int root) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_setfield(L, root, "casts");
    SetInteger(L, root, "nextCast", nextCast);
}

void ScriptSpellHost::Restore(int root) {
    if (lua_getfield(L, root, "casts") == LUA_TTABLE) {
        luaL_unref(L, LUA_REGISTRYINDEX, casts);
        casts = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    if (const lua_Integer next = IntegerField(L, root, "nextCast"); next > 0) nextCast = static_cast<int>(next);
}

// cast:After(ticks, name [, payload]): module.name(cast, payload) after `ticks` ticks (at least 1, whole ticks).
int ScriptSpellHost::After(lua_State* L) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_settop(L, 4);   // the payload nil when none is given
    const lua_Number ticks = luaL_checknumber(L, 2);
    luaL_checkstring(L, 3);
    const int entry = CheckEntry(L, host->casts, "After");
    const lua_Integer delay =
        ticks >= 1.0 ? static_cast<lua_Integer>(std::floor(std::min(ticks, kMaxDelay))) : lua_Integer{1};
    const lua_Integer seq = IntegerField(L, entry, "seq") + 1;
    SetInteger(L, entry, "seq", seq);
    lua_getfield(L, entry, "timers");
    const int timers = lua_gettop(L);
    lua_createtable(L, 0, 4);
    const int timer = lua_gettop(L);
    SetInteger(L, timer, "due", IntegerField(L, entry, "tick") + delay);
    SetInteger(L, timer, "seq", seq);
    lua_pushvalue(L, 3);
    lua_setfield(L, timer, "name");
    lua_pushvalue(L, 4);
    lua_setfield(L, timer, "payload");
    lua_rawseti(L, timers, static_cast<lua_Integer>(lua_rawlen(L, timers)) + 1);
    return 0;
}

// cast:Finish(): the cast ends after the hook that called it.
int ScriptSpellHost::Finish(lua_State* L) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int entry = CheckEntry(L, host->casts, "Finish");
    lua_pushboolean(L, true);
    lua_setfield(L, entry, "finished");
    return 0;
}

void ScriptSpellHost::PushModule(const SpellEffect& effect) {
    const std::string& file = ScriptOf(effect);
    std::string key = file;
    text::LowerInPlace(key.data(), key.size());
    lua_rawgeti(L, LUA_REGISTRYINDEX, modules);
    const int loaded = lua_gettop(L);
    if (lua_getfield(L, loaded, key.c_str()) != LUA_TNIL) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        lua_remove(L, loaded);
        return;
    }
    lua_pop(L, 1);

    // The file runs in globals of its own that read the state's, with no bindings: what it returns is the module.
    std::string text, error;
    if (file.empty() || !readModule || !readModule(file, &text)) {
        error = "no such file";
    } else {
        std::string_view source = text;
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);   // a UTF-8 byte order mark
        const std::string chunk = "=" + file;
        if (luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t") == LUA_OK) {
            lua_newtable(L);
            lua_createtable(L, 0, 1);
            lua_pushglobaltable(L);
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);
            lua_setupvalue(L, -2, 1);
            const bool worldWasOff = scheduler.lua.worldOff;
            scheduler.lua.worldOff = true;
            const int status = lua_pcall(L, 0, 1, 0);
            scheduler.lua.worldOff = worldWasOff;
            if (status != LUA_OK) error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(no message)";
            else if (!lua_istable(L, -1)) error = "it gives no table of hooks";
        } else {
            error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(no message)";
        }
        if (!error.empty()) lua_pop(L, 1);
    }
    if (!error.empty()) {
        Report(std::format("{}\\{} did not load: {}", kFolder, file, error));
        lua_pushboolean(L, false);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, loaded, key.c_str());
    lua_remove(L, loaded);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
}

void ScriptSpellHost::PushEntry(lua_State* state, int key) {
    lua_rawgeti(state, LUA_REGISTRYINDEX, casts);
    lua_rawgeti(state, -1, key);
    lua_remove(state, -2);
}

void ScriptSpellHost::PushCast(const SpellEffect& effect, int entry) {
    lua_createtable(L, 0, 16);
    const int cast = lua_gettop(L);
    const Spell& spell = *effect.HeldSpell();
    const itemdb::SpellRecord* record = spell.GetPrototype();
    SetInteger(L, cast, "id", effect.castKey);
    lua_pushstring(L, record ? record->code.c_str() : "");
    lua_setfield(L, cast, "code");
    scriptlua::PushObject(L, effect.caster.Get());
    lua_setfield(L, cast, "caster");
    scriptlua::PushObject(L, effect.target.Get());
    lua_setfield(L, cast, "target");
    PushPoint(L, effect.position);
    lua_setfield(L, cast, "from");
    PushPoint(L, effect.targetPoint);
    lua_setfield(L, cast, "point");
    SetInteger(L, cast, "tick", IntegerField(L, entry, "tick"));
    SetNumber(L, cast, "damage", spell.damage);
    SetNumber(L, cast, "range", spell.range);
    SetNumber(L, cast, "radius", spell.radius);
    SetInteger(L, cast, "duration", spell.duration);
    SetInteger(L, cast, "targetCount", spell.targetCount);
    SetNumber(L, cast, "speed", spell.speed);
    SetNumber(L, cast, "mana", spell.manaCost);
    lua_getfield(L, entry, "data");
    lua_setfield(L, cast, "data");
    lua_rawgeti(L, LUA_REGISTRYINDEX, castMeta);
    lua_setmetatable(L, cast);
}

ScriptSpellHost::Hook ScriptSpellHost::Call(const SpellEffect& effect, const char* name, int arguments) {
    const int base = lua_gettop(L) - arguments;
    PushEntry(L, effect.castKey);
    const int entry = lua_gettop(L);
    PushModule(effect);
    if (!lua_istable(L, entry) || !lua_istable(L, -1) || lua_getfield(L, -1, name) != LUA_TFUNCTION) {
        lua_settop(L, base);
        return Hook::Absent;
    }
    // Below the arguments: the cast value (kept for its data after the call), the function, the cast value.
    PushCast(effect, entry);
    lua_pushvalue(L, -1);
    lua_insert(L, base + 1);
    lua_insert(L, base + 2);
    lua_insert(L, base + 2);
    lua_settop(L, base + 3 + arguments);

    ScriptRun* const outer = scheduler.lua.currentRun;
    run = ScriptRun{};
    scheduler.lua.currentRun = &run;
    const int status = lua_pcall(L, 1 + arguments, 0, 0);
    scheduler.lua.currentRun = outer;
    if (status != LUA_OK) {
        Report(lua_tostring(L, -1) ? lua_tostring(L, -1) : "(no message)");
        lua_settop(L, base);
        return Hook::Failed;
    }
    // The hook may have given the cast a new data table.
    PushEntry(L, effect.castKey);
    if (lua_istable(L, -1)) {
        lua_getfield(L, base + 1, "data");
        lua_setfield(L, -2, "data");
    }
    lua_settop(L, base);
    return Hook::Ran;
}

bool ScriptSpellHost::Settle(SpellEffect& effect, Hook result) {
    if (result == Hook::Failed) {
        End(effect, "error");
        return false;
    }
    PushEntry(L, effect.castKey);
    const int entry = lua_gettop(L);
    if (!lua_istable(L, entry)) {
        lua_pop(L, 1);
        Drop(effect);
        return false;
    }
    const bool finished = EntryFlag(entry, "finished");
    const lua_Integer tick = IntegerField(L, entry, "tick");
    lua_getfield(L, entry, "timers");
    const bool timers = lua_istable(L, -1) && lua_rawlen(L, -1) > 0;
    lua_pop(L, 2);
    const std::uint32_t duration = effect.HeldSpell()->duration;
    const lua_Integer cap = duration > 0 ? static_cast<lua_Integer>(duration) : kDefaultTickCap;

    const char* reason = nullptr;
    if (finished) {
        reason = "finished";
    } else if (tick >= cap) {
        reason = "limit";
    } else if (!timers) {
        PushModule(effect);
        const bool ticks = lua_istable(L, -1) && lua_getfield(L, -1, "OnTick") == LUA_TFUNCTION;
        lua_settop(L, entry - 1);
        if (!ticks) reason = "idle";
    }
    if (!reason) return true;
    End(effect, reason);
    return false;
}

void ScriptSpellHost::End(SpellEffect& effect, const char* reason) {
    lua_pushstring(L, reason);
    Call(effect, "OnEnd", 1);
    Drop(effect);
}

void ScriptSpellHost::Drop(SpellEffect& effect) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_pushnil(L);
    lua_rawseti(L, -2, effect.castKey);
    lua_pop(L, 1);
    live.erase(effect.castKey);
    effect.host = nullptr;
}

bool ScriptSpellHost::EntryFlag(int entry, const char* name) {
    lua_getfield(L, entry, name);
    const bool flag = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return flag;
}

void ScriptSpellHost::Report(const std::string& message) {
    if (scheduler.report) scheduler.report(message);
}
