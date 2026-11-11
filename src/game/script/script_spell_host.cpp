// The scripted spells' runtime in the zone's server Lua state. See script_spell_host.h and docs/notes/items_spells.md,
// "Scripted spells".
#include "game/script/script_spell_host.h"

#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_scheduler.h"
#include "game/ai/unit_ai.h"
#include "engine/data/record_names.h"
#include "game/spells/spell.h"
#include "game/spells/spell_effect_internal.h"
#include "game/players/player.h"
#include "game/units/acks_db.h"
#include "game/units/unit.h"

#include "lib/float_select.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <vector>

namespace {

constexpr const char* kFolder = "Res\\spells";
// The ticks a cast lives at most when its spell's duration does not say (Settle).
constexpr lua_Integer kDefaultTickCap = 600;
// The longest delay a timer takes, in ticks.
constexpr double kMaxDelay = 1e9;
// Notice's decay of what the unit noticed (the fireworks' fright's).
constexpr int kNoticeDecay = -1;

// The file through the mod layers, else the game's own (assets/spells, built in below them).
bool ReadFromLayers(const std::string& file, std::string* text) {
    for (const platform::FolderLayer& layer : platform::FilesByLayer(kFolder, {file}))
        for (const platform::LayerFile& found : layer.files)
            if (platform::ReadLayerFile(found.path, text)) return true;
    const std::string_view builtin = itemdb::BuiltinSpellFile(file);
    if (builtin.empty()) return false;
    text->assign(builtin);
    return true;
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

// The number of `table`'s field `name` (an index when `name` is null and `index` > 0) as a float; `fallback` when it
// is nil, a Lua error when it is no number.
float FloatField(lua_State* L, int table, const char* name, float fallback, int index = 0) {
    if (name) lua_getfield(L, table, name);
    else lua_rawgeti(L, table, index);
    float value = fallback;
    if (!lua_isnil(L, -1)) {
        int isNumber = 0;
        const lua_Number number = lua_tonumberx(L, -1, &isNumber);
        if (!isNumber) luaL_error(L, "%s: a number expected", name ? name : "a colour's part");
        value = scriptlua::FromLuaNumber(number);
    }
    lua_pop(L, 1);
    return value;
}

// The whole number of `table`'s field `name` (truncated), `fallback` when it is nil.
int IntField(lua_State* L, int table, const char* name, int fallback) {
    const float value = FloatField(L, table, name, static_cast<float>(fallback));
    if (!(value > -2147483648.0f && value < 2147483648.0f)) return fallback;
    return static_cast<int>(value);
}

bool BoolField(lua_State* L, int table, const char* name, bool fallback) {
    lua_getfield(L, table, name);
    const bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

// The options table at `index` (none or nil: an empty one pushed in its place); a Lua error for anything else.
void CheckOptions(lua_State* L, int index) {
    if (lua_gettop(L) < index) lua_settop(L, index);
    if (lua_isnil(L, index)) {
        lua_newtable(L);
        lua_replace(L, index);
    }
    luaL_checktype(L, index, LUA_TTABLE);
}

// A position at `index`: a point table {x, y, z} or an object (its position; *object set). False for nil (and no
// value); a Lua error for anything else, or a nil object.
bool OptPosition(lua_State* L, int index, Vector3* point, MapObject** object) {
    *object = nullptr;
    if (lua_isnoneornil(L, index)) return false;
    if (lua_istable(L, index)) {
        *point = {FloatField(L, index, "x", 0.0f), FloatField(L, index, "y", 0.0f), FloatField(L, index, "z", 0.0f)};
        return true;
    }
    MapObject* found = scriptlua::CheckObject(L, index)->object.Get();
    if (!found) luaL_error(L, "a position expected, got an object that is gone");
    *object = found;
    *point = found->localPosition;
    return true;
}

// OptPosition of a position that must be there.
void CheckPosition(lua_State* L, int index, Vector3* point, MapObject** object, const char* method) {
    if (!OptPosition(L, index, point, object)) luaL_error(L, "%s: a position {x, y, z} or an object expected", method);
}

// The object at `index` (null for nil or one that is gone).
MapObject* OptObject(lua_State* L, int index) { return scriptlua::CheckObject(L, index)->object.Get(); }

// The field `name` of the table at `table` moved to the stack's index `index` (which must be free above the table's).
void FieldTo(lua_State* L, int table, const char* name, int index) {
    lua_getfield(L, table, name);
    lua_replace(L, index);
}

// The damage type named at `index` (recordnames::kDamageTypes); a Lua error for another name.
DamageType CheckDamageType(lua_State* L, int index, const char* method) {
    const char* name = luaL_checkstring(L, index);
    for (std::size_t i = 0; i < std::size(recordnames::kDamageTypes); ++i)
        if (text::CompareIgnoringCase(recordnames::kDamageTypes[i], name) == 0) return static_cast<DamageType>(i);
    luaL_error(L, "%s: no damage type %s", method, name);
    return DamageType::Other;
}

// The spell record an effect kind at `index` names: "own" the cast's spell, else a spell's code (any case); its place
// (MapEffect::type). A Lua error for another name.
std::uint8_t CheckKind(lua_State* L, int index, const SpellEffect& effect, const char* method) {
    const char* name = luaL_checkstring(L, index);
    if (text::CompareIgnoringCase("own", name) == 0) return effect.HeldSpell()->prototypeIndex;
    const std::vector<itemdb::SpellRecord>& records = g_spellDatabase.spells.records;
    for (std::size_t i = 0; i < records.size(); ++i)
        if (text::CompareIgnoringCase(records[i].code, name) == 0) return static_cast<std::uint8_t>(i);
    luaL_error(L, "%s: no spell %s", method, name);
    return 0;
}

// The particle type named at `index` (ScriptParticleTypeByName); a Lua error for an unknown name.
int CheckParticleType(lua_State* L, int index, const char* method) {
    const char* name = luaL_checkstring(L, index);
    int lifetime = 0;
    const int type = ScriptParticleTypeByName(name, &lifetime);
    if (!type) luaL_error(L, "%s: no effect %s", method, name);
    return type;
}

// Where a spell's effects start from an object: a unit's eye, another object's position.
Vector3 StartPoint(MapObject* object) { return object->EyePoint(); }

// An owned object made by a method, its id (the handle a script keeps) pushed.
int PushHandle(lua_State* L, const Effect* object) {
    lua_pushinteger(L, object->id);
    return 1;
}

// The upright plane through `at` along `direction` (the walls' hit): s = 2 / sqrt(x^2 + y^2) (a float), the normal
// (s * -y, s * x, 0) is 2 long, so |d . n| <= 1 holds the points within 0.5 of the plane.
class WallPlane {
public:
    WallPlane(const Vector3& origin, const Vector3& direction) : at(origin) {
        const float x = direction.x;
        const float minusY = -direction.y;
        const float scale = 2.0f / std::sqrt(x * x + minusY * minusY);
        normal = {scale * minusY, scale * x, 0.0f};
    }
    bool Holds(const Vector3& point) const { return !(AbsBits(Dot(point - at, normal)) > kHalfWidth); }

private:
    static constexpr float kHalfWidth = 1.0f;   // in the normal's units: 0.5
    Vector3 at;
    Vector3 normal;
};

// A cast's footprint as its entry keeps it: {at = {x, y, z}, along = {x, y, z}} or {at = {x, y, z}, radius = r}.
void PushFootprint(lua_State* L, const SpellFootprint& footprint) {
    lua_createtable(L, 0, 2);
    const int table = lua_gettop(L);
    PushPoint(L, footprint.at);
    lua_setfield(L, table, "at");
    if (footprint.disc) {
        SetNumber(L, table, "radius", footprint.radius);
    } else {
        PushPoint(L, footprint.along);
        lua_setfield(L, table, "along");
    }
}

// A point field {x, y, z} of the table at `table` (0s when it is not a table).
Vector3 PointField(lua_State* L, int table, const char* name) {
    lua_getfield(L, table, name);
    const int point = lua_gettop(L);
    Vector3 value;
    if (lua_istable(L, point))
        value = {FloatField(L, point, "x", 0.0f), FloatField(L, point, "y", 0.0f), FloatField(L, point, "z", 0.0f)};
    lua_pop(L, 1);
    return value;
}

SpellFootprint ReadFootprint(lua_State* L, int table) {
    SpellFootprint footprint;
    footprint.at = PointField(L, table, "at");
    lua_getfield(L, table, "radius");
    footprint.disc = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (footprint.disc) footprint.radius = FloatField(L, table, "radius", 0.0f);
    else footprint.along = PointField(L, table, "along");
    return footprint;
}

// The cast's hits would affect `object` (SpellEffect::HitUnit's test).
bool Affects(const SpellEffect& effect, MapObject* object) {
    return !effect.caster || effect.HeldSpell()->CanAffect(effect.caster, object);
}

lua_Integer IntegerField(lua_State* L, int table, const char* name) {
    lua_getfield(L, table, name);
    const lua_Integer value = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    return value;
}

// The entry of the cast value `self` (argument 1) of a method, from the host's casts table (its upvalue's registry
// reference) pushed, its key in *key; a Lua error when the cast has ended.
int CheckEntry(lua_State* L, int casts, const char* method, int* key = nullptr) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_getfield(L, 1, "id");
    if (lua_isinteger(L, -1)) {
        if (key) *key = static_cast<int>(lua_tointeger(L, -1));
        lua_rawget(L, -2);
    }
    if (!lua_istable(L, -1)) return luaL_error(L, "%s: the cast has ended", method);
    lua_remove(L, -2);
    return lua_gettop(L);
}

} // namespace

ScriptSpellHost::ScriptSpellHost(ScriptLuaScheduler& owner)
    : readModule(ReadFromLayers), scheduler(owner), L(owner.lua.State()) {
    lua_newtable(L);
    casts = luaL_ref(L, LUA_REGISTRYINDEX);
    // The cast values' metatable and its methods, permanents: a cast value a script keeps in its data persists.
    lua_createtable(L, 0, 1);
    lua_createtable(L, 0, 35);
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
    method("FindUnits", FindUnits);
    method("CanAffect", CanAffect);
    method("Damage", Damage);
    method("Heal", Heal);
    method("Apply", Apply);
    method("Remove", Remove);
    method("Has", Has);
    method("Move", Move);
    method("Bolt", Bolt);
    method("Missile", Missile);
    method("Effect", Effect);
    method("Sound", Sound);
    method("Light", Light);
    method("SetVelocity", SetVelocity);
    method("Eye", Eye);
    method("HitPoint", HitPoint);
    method("IsUnit", IsUnit);
    method("Ground", Ground);
    method("Flight", Flight);
    method("SoundLoop", SoundLoop);
    method("Release", Release);
    method("Noise", Noise);
    method("Across", Across);
    method("Block", Block);
    method("Unblock", Unblock);
    method("Position", Position);
    method("IsDead", IsDead);
    method("IsHurt", IsHurt);
    method("Shout", Shout);
    method("Reveal", Reveal);
    method("Wake", Wake);
    method("Notice", Notice);
    method("Vision", Vision);
    lua_pushvalue(L, methods);
    scheduler.lua.AddPermanent("ei.cast.methods");
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, -1);
    scheduler.lua.AddPermanent("ei.cast");
    castMeta = luaL_ref(L, LUA_REGISTRYINDEX);

    // OnStats' stats value, its metatable and methods, and its effect value, permanents too.
    lua_newtable(L);
    lua_createtable(L, 0, 1);
    lua_createtable(L, 0, 4);
    const int statMethods = lua_gettop(L);
    const auto statMethod = [this, statMethods](const char* name, lua_CFunction function) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, function, 1);
        lua_pushvalue(L, -1);
        scheduler.lua.AddPermanent(std::format("ei.stats.{}", name).c_str());
        lua_setfield(L, statMethods, name);
    };
    statMethod("Add", StatAdd);
    statMethod("Subtract", StatSubtract);
    statMethod("Larger", StatLarger);
    statMethod("Multiply", StatMultiply);
    lua_pushvalue(L, statMethods);
    scheduler.lua.AddPermanent("ei.stats.methods");
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, -1);
    scheduler.lua.AddPermanent("ei.stats.meta");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    scheduler.lua.AddPermanent("ei.stats");
    statsValue = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, -1);
    scheduler.lua.AddPermanent("ei.effect");
    effectValue = luaL_ref(L, LUA_REGISTRYINDEX);
}

ScriptSpellHost::~ScriptSpellHost() {
    for (const auto& [key, effect] : live) effect->host = nullptr;
    for (auto& [key, clock] : clocks) ReleaseValue(clock);
    luaL_unref(L, LUA_REGISTRYINDEX, casts);
    for (const auto& [file, module] : modules) luaL_unref(L, LUA_REGISTRYINDEX, module);
    luaL_unref(L, LUA_REGISTRYINDEX, castMeta);
    luaL_unref(L, LUA_REGISTRYINDEX, statsValue);
    luaL_unref(L, LUA_REGISTRYINDEX, effectValue);
}

void ScriptSpellHost::NewEntry(SpellEffect& effect, int key) {
    effect.castKey = key;
    effect.host = this;
    live[key] = &effect;
    NewClock(key);
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
}

bool ScriptSpellHost::Cast(SpellEffect& effect) {
    NewEntry(effect, nextCast++);
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
    Clock& clock = clocks[effect.castKey];
    if (!clock.known) {
        Flush(effect.castKey, clock);
        PushEntry(L, effect.castKey);
        if (!lua_istable(L, -1)) {
            lua_settop(L, base);
            Drop(effect);
            return false;
        }
        clock.tick = IntegerField(L, base + 1, "tick");
        lua_settop(L, base);
    }
    const lua_Integer tick = ++clock.tick;
    clock.stale = true;

    // The owned objects' lives, then OnHit(cast, target, payload, point) for each missile that arrived, in order.
    std::vector<SpellArrival> arrived;
    effect.TickOwned(&arrived);
    if (arrived.empty() && Idle(effect, clock)) return true;
    Flush(effect.castKey, clock);
    for (const SpellArrival& arrival : arrived) {
        PushEntry(L, effect.castKey);
        const int entry = base + 1;
        if (!lua_istable(L, entry)) {
            lua_settop(L, base);
            return false;
        }
        scriptlua::PushObject(L, effect.world->ObjectWithId(arrival.targetId));
        // The payload out of the cast's missiles (each kept wrapped in a table: nil is a payload too).
        lua_getfield(L, entry, "missiles");
        const int missiles = base + 3;
        lua_pushnil(L);
        if (lua_istable(L, missiles)) {
            if (lua_rawgeti(L, missiles, arrival.payload) == LUA_TTABLE) {
                lua_rawgeti(L, -1, 1);
                lua_replace(L, missiles + 1);
            }
            lua_settop(L, missiles + 1);
            lua_pushnil(L);
            lua_rawseti(L, missiles, arrival.payload);
        }
        lua_remove(L, missiles);
        PushPoint(L, arrival.point);
        lua_remove(L, entry);
        const Hook result = Call(effect, "OnHit", 3);
        if (result == Hook::Failed || Finished(effect, clock)) return Settle(effect, result);
    }

    // The due timers one at a time by their due tick, then the order they were set in: a timer's function may set
    // more (none due before the next tick). A known clock says whether one is due, and the walk keeps its `due` (the
    // earliest of the timers left; After lowers it), so one due timer takes one walk and a tick without any none.
    while (!clock.known || (clock.due && clock.due <= tick)) {
        PushEntry(L, effect.castKey);
        const int entry = base + 1;
        if (!lua_istable(L, entry)) {
            lua_settop(L, base);
            return false;
        }
        lua_getfield(L, entry, "timers");
        const int timers = base + 2;
        const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, timers));
        // The best is the earliest due, so the timers left start at the second lowest due.
        lua_Integer best = 0, bestDue = 0, bestSeq = 0, lowest = 0, second = 0;
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, timers, i);
            const lua_Integer due = IntegerField(L, -1, "due");
            const lua_Integer seq = IntegerField(L, -1, "seq");
            lua_pop(L, 1);
            if (i == 1 || due < lowest) {
                second = lowest;
                lowest = due;
            } else if (i == 2 || due < second) {
                second = due;
            }
            if (due > tick || (best && (due > bestDue || (due == bestDue && seq > bestSeq)))) continue;
            best = i;
            bestDue = due;
            bestSeq = seq;
        }
        if (!best) {
            clock.due = count > 0 ? lowest : 0;
            lua_settop(L, base);
            break;
        }
        // One at 0 or below is due now (Settle's walk).
        clock.due = count > 1 ? (second > 0 ? second : tick) : 0;
        lua_rawgeti(L, timers, best);
        lua_getfield(L, -1, "name");
        lua_getfield(L, -2, "payload");
        for (lua_Integer i = best; i < count; ++i) {
            lua_rawgeti(L, timers, i + 1);
            lua_rawseti(L, timers, i);
        }
        lua_pushnil(L);
        lua_rawseti(L, timers, count);
        // The name below the payload, kept on the stack while the call runs.
        lua_copy(L, base + 4, base + 1);
        lua_copy(L, base + 5, base + 2);
        lua_settop(L, base + 2);
        const char* name = lua_isstring(L, base + 1) ? lua_tostring(L, base + 1) : "";
        Hook result = Call(effect, name, 1);
        if (result == Hook::Absent) {
            Report(std::format("{}: no function {} for a timer", ScriptOf(effect), name));
            result = Hook::Failed;
        }
        lua_settop(L, base);
        if (result == Hook::Failed || Finished(effect, clock)) return Settle(effect, result);
    }
    return Settle(effect, Call(effect, "OnTick", 0));
}

void ScriptSpellHost::Forget(SpellEffect& effect) { Drop(effect); }

void ScriptSpellHost::EffectTotals(std::vector<EffectKindStats>& kinds, EffectStats& totals) {
    totals = {};
    const bool worldWasOff = scheduler.lua.worldOff;
    scheduler.lua.worldOff = true;
    const int base = lua_gettop(L);
    for (EffectKindStats& kind : kinds) {
        kind.touched.reset();
        const itemdb::SpellRecord* record = itemdb::SpellAt(static_cast<std::uint32_t>(kind.kind));
        if (!record || record->script.empty()) continue;
        PushModule(record->script);
        if (!lua_istable(L, -1) || lua_getfield(L, -1, "OnStats") != LUA_TFUNCTION) {
            lua_settop(L, base);
            continue;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, effectValue);
        std::string code = record->code;
        text::LowerInPlace(code.data(), code.size());
        lua_pushstring(L, code.c_str());
        lua_setfield(L, -2, "kind");
        lua_pushnumber(L, scriptlua::ToLuaNumber(kind.value));
        lua_setfield(L, -2, "value");
        lua_rawgeti(L, LUA_REGISTRYINDEX, statsValue);
        const EffectStats before = totals;
        statsTotals = &totals;
        statsKind = &kind;
        const int status = lua_pcall(L, 2, 0, 0);
        statsTotals = nullptr;
        statsKind = nullptr;
        if (status != LUA_OK) {
            Report(lua_tostring(L, -1) ? lua_tostring(L, -1) : "(no message)");
            totals = before;
            kind.touched.reset();
        }
        lua_settop(L, base);
    }
    scheduler.lua.worldOff = worldWasOff;
}

int ScriptSpellHost::Stat(lua_State* L, StatOperation operation, const char* method) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* name = luaL_checkstring(L, 2);
    const float x = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    if (!host->statsTotals) return luaL_error(L, "stats:%s: only while OnStats runs", method);
    const auto found = std::find(kEffectStatNames.begin(), kEffectStatNames.end(), std::string_view(name));
    if (found == kEffectStatNames.end()) return luaL_error(L, "stats:%s: no stat %s", method, name);
    const auto index = static_cast<std::size_t>(found - kEffectStatNames.begin());
    float& total = (*host->statsTotals)[index];
    switch (operation) {
    case StatOperation::Add: total = total + x; break;
    case StatOperation::Subtract: total = total - x; break;
    case StatOperation::Larger: total = MaxKeepB(total, x); break;
    case StatOperation::Multiply: total = total * x; break;
    }
    host->statsKind->touched.set(index);
    return 0;
}

int ScriptSpellHost::StatAdd(lua_State* L) { return Stat(L, StatOperation::Add, "Add"); }
int ScriptSpellHost::StatSubtract(lua_State* L) { return Stat(L, StatOperation::Subtract, "Subtract"); }
int ScriptSpellHost::StatLarger(lua_State* L) { return Stat(L, StatOperation::Larger, "Larger"); }
int ScriptSpellHost::StatMultiply(lua_State* L) { return Stat(L, StatOperation::Multiply, "Multiply"); }

ScriptSpellHost::Clock& ScriptSpellHost::NewClock(int key) {
    Clock& clock = clocks[key];
    ReleaseValue(clock);
    clock = Clock{};
    return clock;
}

void ScriptSpellHost::ReleaseValue(Clock& clock) {
    if (clock.value == LUA_NOREF) return;
    luaL_unref(L, LUA_REGISTRYINDEX, clock.value);
    clock.value = LUA_NOREF;
}

void ScriptSpellHost::Flush(int key, Clock& clock) {
    if (!clock.stale) return;
    clock.stale = false;
    PushEntry(L, key);
    if (lua_istable(L, -1)) SetInteger(L, lua_gettop(L), "tick", clock.tick);
    lua_pop(L, 1);
}

bool ScriptSpellHost::Idle(const SpellEffect& effect, const Clock& clock) {
    if (!clock.known || clock.module < 0 || (clock.due && clock.due <= clock.tick)) return false;
    if (clock.tick >= TickCap(effect, false) || (!clock.due && !effect.OwnsPending())) return false;
    // OnTick looked up as Call does; a module with a metatable could answer anything, so it takes the walk.
    const int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, clock.module);
    const bool idle = !lua_getmetatable(L, -1) && lua_getfield(L, -1, "OnTick") != LUA_TFUNCTION;
    lua_settop(L, base);
    return idle;
}

std::int64_t ScriptSpellHost::TickCap(const SpellEffect& effect, bool ticks) {
    // A cast with OnTick lives at most its spell's duration; one paced by its timers alone at most kDefaultTickCap
    // ticks, or its duration when that is longer (the game's spells' durations are target counts or the ticks of
    // what they leave behind, which their timers outlast); unless the record's max_ticks says (0: no limit).
    const auto duration = static_cast<lua_Integer>(effect.HeldSpell()->duration);
    const itemdb::SpellRecord* record = effect.HeldSpell()->GetPrototype();
    const int maxTicks = record ? record->maxTicks : -1;
    if (maxTicks >= 0) return maxTicks > 0 ? maxTicks : std::numeric_limits<std::int64_t>::max();
    return ticks ? (duration > 0 ? duration : kDefaultTickCap) : std::max(duration, kDefaultTickCap);
}

bool ScriptSpellHost::Adopt(SpellEffect& effect) {
    PushEntry(L, effect.castKey);
    const bool found = lua_istable(L, -1);
    lua_pop(L, 1);
    if (!found) return false;
    effect.host = this;
    live[effect.castKey] = &effect;
    NewClock(effect.castKey);
    nextCast = std::max(nextCast, effect.castKey + 1);
    // The footprint the cast blocked (the AI map is not saved).
    PushEntry(L, effect.castKey);
    if (lua_getfield(L, -1, "block") == LUA_TTABLE) effect.Block(ReadFootprint(L, lua_gettop(L)));
    lua_pop(L, 2);
    return true;
}

bool ScriptSpellHost::Resume(SpellEffect& effect) {
    const int base = lua_gettop(L);
    PushModule(ScriptOf(effect));
    const bool resumes = lua_istable(L, -1) && lua_getfield(L, -1, "OnResume") == LUA_TFUNCTION;
    lua_settop(L, base);
    if (!resumes) return false;
    NewEntry(effect, nextCast++);

    // The C++'s objects become the cast's, in their slots' order: a missile flies on under a payload key (nil).
    PushEntry(L, effect.castKey);
    const int entry = lua_gettop(L);
    lua_newtable(L);
    const int missiles = lua_gettop(L);
    const auto adopt = [&](::Effect* object) {
        if (!object) return;
        int payload = 0;
        if (object->GetClassId() == ClassId::EffectArrow) {
            payload = static_cast<int>(IntegerField(L, entry, "seq") + 1);
            SetInteger(L, entry, "seq", payload);
            lua_createtable(L, 1, 0);
            lua_rawseti(L, missiles, payload);
        }
        SpellOwned& owned = effect.owned.emplace_back();
        owned.object.Assign(object);
        owned.payload = payload;
    };
    adopt(effect.visual.Get());
    adopt(effect.sound.Get());
    adopt(effect.light.Get());
    lua_setfield(L, entry, "missiles");
    lua_settop(L, base);

    // `old`: the C++'s state and count, and the handles of the objects it held.
    lua_createtable(L, 0, 5);
    const int old = lua_gettop(L);
    lua_pushstring(L, effect.state == spellfx::kStateMissile ? "missile" : "lasting");
    lua_setfield(L, old, "state");
    SetInteger(L, old, "ticks", effect.timer);
    if (effect.visual) SetInteger(L, old, "visual", effect.visual->id);
    if (effect.sound) SetInteger(L, old, "sound", effect.sound->id);
    if (effect.light) SetInteger(L, old, "light", effect.light->id);
    if (::Effect* visual = effect.visual.Get()) {
        if (visual->GetClassId() == ClassId::EffectWall) {
            PushPoint(L, static_cast<EffectWall*>(visual)->direction);
            lua_setfield(L, old, "direction");
        } else if (visual->GetClassId() == ClassId::EffectLightning) {
            const auto* bolt = static_cast<EffectLightning*>(visual);
            PushPoint(L, bolt->to - bolt->from);
            lua_setfield(L, old, "direction");
        }
    }
    effect.visual.Reset();
    effect.sound.Reset();
    effect.light.Reset();
    const bool worldWasOff = scheduler.lua.worldOff;
    scheduler.lua.worldOff = true;
    resuming = true;
    const bool lives = Settle(effect, Call(effect, "OnResume", 1));
    resuming = false;
    scheduler.lua.worldOff = worldWasOff;
    effect.state = lives ? spellfx::kStateScripted : spellfx::kStateEnded;
    return true;
}

void ScriptSpellHost::Save(int root) {
    for (auto& [key, clock] : clocks) Flush(key, clock);
    lua_rawgeti(L, LUA_REGISTRYINDEX, casts);
    lua_setfield(L, root, "casts");
    // The key after the live casts' (1 with none), not the count of casts made: a save then holds only what lives,
    // and one with no cast going is the same however many came before.
    SetInteger(L, root, "nextCast", live.empty() ? 1 : live.rbegin()->first + 1);
}

void ScriptSpellHost::Restore(int root) {
    if (lua_getfield(L, root, "casts") == LUA_TTABLE) {
        luaL_unref(L, LUA_REGISTRYINDEX, casts);
        casts = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    if (const lua_Integer next = IntegerField(L, root, "nextCast"); next > 0) nextCast = static_cast<int>(next);
    for (auto& [key, clock] : clocks) {
        ReleaseValue(clock);
        clock = Clock{};
    }
}

// cast:After(ticks, name [, payload]): module.name(cast, payload) after `ticks` ticks (at least 1, whole ticks).
int ScriptSpellHost::After(lua_State* L) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_settop(L, 4);   // the payload nil when none is given
    const lua_Number ticks = luaL_checknumber(L, 2);
    luaL_checkstring(L, 3);
    int key = 0;
    const int entry = CheckEntry(L, host->casts, "After", &key);
    // The cast may be another's (a cast value kept in a script's data), between its ticks: its tick written first
    // (a known clock then holds the entry's).
    const auto clock = host->clocks.find(key);
    const bool known = clock != host->clocks.end() && clock->second.known;
    if (clock != host->clocks.end()) host->Flush(clock->first, clock->second);
    const lua_Integer delay =
        ticks >= 1.0 ? static_cast<lua_Integer>(std::floor(std::min(ticks, kMaxDelay))) : lua_Integer{1};
    const lua_Integer seq = IntegerField(L, entry, "seq") + 1;
    SetInteger(L, entry, "seq", seq);
    lua_getfield(L, entry, "timers");
    const int timers = lua_gettop(L);
    lua_createtable(L, 0, 4);
    const int timer = lua_gettop(L);
    const lua_Integer due = (known ? clock->second.tick : IntegerField(L, entry, "tick")) + delay;
    if (clock != host->clocks.end() && (!clock->second.due || due < clock->second.due)) clock->second.due = due;
    SetInteger(L, timer, "due", due);
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
    int key = 0;
    const int entry = CheckEntry(L, host->casts, "Finish", &key);
    if (const auto clock = host->clocks.find(key); clock != host->clocks.end()) clock->second.known = false;
    lua_pushboolean(L, true);
    lua_setfield(L, entry, "finished");
    return 0;
}

SpellEffect& ScriptSpellHost::Self(lua_State* L, const char* method, bool inResume) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (host->scheduler.lua.worldOff && !(inResume && host->resuming))
        luaL_error(L, "%s: no bindings at a spell file's top level or in OnResume", method);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "id");
    const lua_Integer key = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    const auto found = host->live.find(static_cast<int>(key));
    if (found == host->live.end() || !found->second->world) luaL_error(L, "%s: the cast has ended", method);
    return *found->second;
}

bool ScriptSpellHost::Hand(std::string* data, std::map<int, SpellEffect*>* effects) {
    if (live.empty()) return false;
    lua_createtable(L, 0, 2);
    Save(lua_gettop(L));
    std::string error;
    if (!scheduler.lua.Persist(data, &error)) {
        Report(std::format("the spell casts going did not carry over a script reset ({}); they end", error));
        return false;
    }
    *effects = std::move(live);
    live.clear();
    for (auto& [key, clock] : clocks) ReleaseValue(clock);
    clocks.clear();
    return true;
}

void ScriptSpellHost::Take(std::string_view data, const std::map<int, SpellEffect*>& effects) {
    std::string error;
    if (!scheduler.lua.Unpersist(data, &error)) {
        Report(std::format("the spell casts going did not carry over a script reset ({}); they end", error));
        for (const auto& [key, effect] : effects) effect->host = nullptr;
        return;
    }
    Restore(lua_gettop(L));
    lua_pop(L, 1);
    for (const auto& [key, effect] : effects) {
        effect->host = this;
        live[key] = effect;
        NewClock(key);
    }
}

// cast:FindUnits(at, radius [, filter]): the units within `radius` of `at`, nearest first (then by id), that pass
// every key the filter sets: enemies / friends (of the caster: either regards the other's side as hostile; with no
// caster every unit is an enemy), alive, area (those area spells hit), sight (the line of sight the spell needs, from
// `at`), affect (the spell's target flags let it hit them), exclude (a set of ids: {[id] = true}), along (a direction
// {x, y, z}: those within 0.5 of the upright plane through `at` along it, as the game's walls hit). `unsorted`: in the
// sector map's order instead, the order the game's area spells hit in.
int ScriptSpellHost::FindUnits(lua_State* L) {
    SpellEffect& effect = Self(L, "FindUnits");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "FindUnits");
    const float radius = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    CheckOptions(L, 4);
    const bool enemies = BoolField(L, 4, "enemies", false);
    const bool friends = BoolField(L, 4, "friends", false);
    const bool alive = BoolField(L, 4, "alive", false);
    const bool area = BoolField(L, 4, "area", false);
    const bool sight = BoolField(L, 4, "sight", false);
    const bool affect = BoolField(L, 4, "affect", false);
    const bool unsorted = BoolField(L, 4, "unsorted", false);
    lua_settop(L, 4);
    lua_getfield(L, 4, "along");
    Vector3 along;
    MapObject* alongObject = nullptr;
    const bool plane = OptPosition(L, 5, &along, &alongObject);
    lua_getfield(L, 4, "exclude");
    const bool excludes = lua_istable(L, 6);

    lua_newtable(L);
    const int result = lua_gettop(L);
    Unit* const caster = effect.caster;
    std::vector<Unit*> units = effect.world->aiMap.sectorMap.FindUnitsInRadius(at, radius);
    std::optional<WallPlane> wall;
    if (plane) wall.emplace(at, along);
    std::erase_if(units, [&](Unit* unit) {
        if (wall && !wall->Holds(unit->localPosition)) return true;
        if (enemies || friends) {
            const bool hostile = !caster || SpellHostile(caster, unit);
            if ((enemies && !hostile) || (friends && hostile)) return true;
        }
        if (alive && unit->IsDead()) return true;
        if (area && !AcceptsAreaSpells(unit)) return true;
        if (sight && !SpellHasLineOfSight(effect.world, effect.HeldSpell(), caster, at, unit, unit->localPosition))
            return true;
        if (affect && !Affects(effect, unit)) return true;
        if (excludes) {
            lua_rawgeti(L, 6, unit->id);
            const bool excluded = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            if (excluded) return true;
        }
        return false;
    });
    if (!unsorted) {
        std::ranges::sort(units, [&at](const Unit* a, const Unit* b) {
            const float da = LengthSquared(a->localPosition - at), db = LengthSquared(b->localPosition - at);
            if (da != db) return da < db;
            return a->id < b->id;
        });
    }
    for (std::size_t i = 0; i < units.size(); ++i) {
        scriptlua::PushObject(L, units[i]);
        lua_rawseti(L, result, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// cast:CanAffect(unit): the cast's hits would affect it (the spell's target flags; anything without a caster).
int ScriptSpellHost::CanAffect(lua_State* L) {
    SpellEffect& effect = Self(L, "CanAffect");
    MapObject* object = OptObject(L, 2);
    lua_pushboolean(L, object && Affects(effect, object));
    return 1;
}

// cast:Damage(unit, amount, type [, {area, noCredit, split}]): a spell's hit on a unit (SpellEffect::HitUnit: only
// what the spell may affect; the caster has the credit unless noCredit), `type` a damage type's name, `area` a hit
// without a direction (the walls' and clouds'), `split` the number of targets sharing it (more than 1: the amount and
// the hit's multiplier x 4 / split, as the game's area spells share theirs by the duration). The hit points the unit
// lost; another object is not hit, as by the game's spells.
int ScriptSpellHost::Damage(lua_State* L) {
    SpellEffect& effect = Self(L, "Damage");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    const float amount = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    const DamageType type = CheckDamageType(L, 4, "Damage");
    CheckOptions(L, 5);
    const int hitKind = BoolField(L, 5, "area", false) ? 1 : 0;
    const bool noCredit = BoolField(L, 5, "noCredit", false);
    const int split = IntField(L, 5, "split", 1);
    float lost = 0.0f;
    if (unit) {
        DamageInfo hit = SpellEffect::TypedHit(type, amount, hitKind);
        SpellEffect::SplitHit(&hit, split);
        effect.flags = noCredit ? effect.flags | 1u : effect.flags & ~1u;
        const float before = unit->Vitals().hitPoints;
        effect.HitUnit(&hit, unit);
        lost = before - unit->Vitals().hitPoints;
    }
    lua_pushnumber(L, scriptlua::ToLuaNumber(lost));
    return 1;
}

// cast:Heal(unit, amount): the unit's controller heals it (the healing spell's path). The hit points it gained.
int ScriptSpellHost::Heal(lua_State* L) {
    Self(L, "Heal");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    const float amount = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    float gained = 0.0f;
    if (unit) {
        const float before = unit->Vitals().hitPoints;
        unit->controller->Heal(amount);
        gained = unit->Vitals().hitPoints - before;
    }
    lua_pushnumber(L, scriptlua::ToLuaNumber(gained));
    return 1;
}

// cast:Apply(unit, kind [, {ticks, power, value, strength}]): an active effect of the spell `kind` names (a spell's
// code: its stat effect and its look, or "own": the cast's spell) on the unit, as a buff spell puts its own
// (SpellEffect::BuffRecord: the cast's duration, power x 16, damage and radius unless the options say otherwise).
int ScriptSpellHost::Apply(lua_State* L) {
    SpellEffect& effect = Self(L, "Apply");
    MapObject* object = OptObject(L, 2);
    const std::uint8_t type = CheckKind(L, 3, effect, "Apply");
    CheckOptions(L, 4);
    MapEffect record = effect.BuffRecord();
    record.type = type;
    record.ticksLeft = IntField(L, 4, "ticks", record.ticksLeft);
    record.power = static_cast<std::uint8_t>(std::clamp(IntField(L, 4, "power", record.power), 0, 255));
    record.value = FloatField(L, 4, "value", record.value);
    record.strength = FloatField(L, 4, "strength", record.strength);
    if (object) object->AddEffect(&record);
    return 0;
}

// cast:Remove(unit, kind): the unit's active effects of that spell removed, each as one that ran out; how many.
int ScriptSpellHost::Remove(lua_State* L) {
    SpellEffect& effect = Self(L, "Remove");
    MapObject* object = OptObject(L, 2);
    const std::uint8_t type = CheckKind(L, 3, effect, "Remove");
    lua_pushinteger(L, object ? object->RemoveEffects(type) : 0);
    return 1;
}

// cast:Has(unit, kind): the unit has an active effect of that spell.
int ScriptSpellHost::Has(lua_State* L) {
    SpellEffect& effect = Self(L, "Has");
    MapObject* object = OptObject(L, 2);
    const std::uint8_t type = CheckKind(L, 3, effect, "Has");
    bool has = false;
    if (object)
        for (const MapEffect& active : object->effects) has = has || active.type == type;
    lua_pushboolean(L, has);
    return 1;
}

// cast:Move(unit, to [, {check = true}]): the unit to the point at its own height (the teleport's path), unless
// `check` and the AI map does not let it stand there. True when it moved.
int ScriptSpellHost::Move(lua_State* L) {
    SpellEffect& effect = Self(L, "Move");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    Vector3 to;
    MapObject* toObject = nullptr;
    CheckPosition(L, 3, &to, &toObject, "Move");
    CheckOptions(L, 4);
    const bool check = BoolField(L, 4, "check", true);
    bool moved = false;
    if (unit) {
        to.z = unit->localPosition.z;
        moved = effect.MoveUnit(unit, to, check);
        if (moved) unit->ServerController()->ResyncWithUnit();
    }
    lua_pushboolean(L, moved);
    return 1;
}

// cast:Bolt(from, to [, {width, lifetime, demon}]): a lightning bolt between two positions; an end that is an object
// follows it (a unit's from its eye, to its top as the lightning spell's). Its width is the lightning's (power x 5)
// unless given, `demon` the curse magic's look; it lasts `lifetime` ticks, else as long as the cast. Its handle.
int ScriptSpellHost::Bolt(lua_State* L) {
    SpellEffect& effect = Self(L, "Bolt");
    Vector3 from, to;
    MapObject* fromObject = nullptr;
    MapObject* toObject = nullptr;
    CheckPosition(L, 2, &from, &fromObject, "Bolt");
    CheckPosition(L, 3, &to, &toObject, "Bolt");
    CheckOptions(L, 4);
    const float width = FloatField(L, 4, "width", spellfx::Power(&effect) * spellfx::kBoltWidthScale);
    const int lifetime = std::max(IntField(L, 4, "lifetime", 0), 0);
    const int flag = BoolField(L, 4, "demon", false) ? 1 : 0;
    if (fromObject) from = StartPoint(fromObject);
    if (toObject) to = effect.LightningEnd(toObject);
    EffectLightning* bolt =
        effect.NewLightning(from, to, fromObject ? fromObject->id : -1, toObject ? toObject->id : -1, width, flag);
    effect.Own(bolt, lifetime, 0);
    return PushHandle(L, bolt);
}

// cast:Missile{to, from, effect, size, sound, payload}: a spell missile flying at the object `to` (the fire arrow's
// EffectArrow, at its speed) from `from` (default: the caster's eye, else the cast's start), carrying the effect
// named (default FireArrow; size default the power) and the looping sound; the light is the spell record's. When it
// arrives, OnHit(cast, target, payload, point) runs (`target` nil when the object is gone). Its handle (nil when the
// world made none).
int ScriptSpellHost::Missile(lua_State* L) {
    SpellEffect& effect = Self(L, "Missile");
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_settop(L, 7);
    FieldTo(L, 2, "to", 3);
    FieldTo(L, 2, "from", 4);
    FieldTo(L, 2, "effect", 5);
    FieldTo(L, 2, "sound", 6);
    FieldTo(L, 2, "payload", 7);
    MapObject* target = OptObject(L, 3);
    if (!target) return luaL_error(L, "Missile: an object to fly at expected (to)");
    Vector3 from;
    MapObject* fromObject = nullptr;
    Unit* launcher = nullptr;
    if (OptPosition(L, 4, &from, &fromObject)) {
        if (fromObject) {
            from = StartPoint(fromObject);
            launcher = UnitOrNull(fromObject);
        }
    } else if (effect.caster) {
        from = StartPoint(effect.caster);
        launcher = effect.caster;
    } else {
        from = effect.position;
    }
    if (lua_isnil(L, 5)) {
        lua_pushliteral(L, "FireArrow");
        lua_replace(L, 5);
    }
    const int particleType = CheckParticleType(L, 5, "Missile");
    const char* sound = lua_isnil(L, 6) ? "" : luaL_checkstring(L, 6);
    const float size = FloatField(L, 2, "size", spellfx::Power(&effect));

    // The payload kept in the cast's missiles under a new key (wrapped: nil is a payload too).
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int entry = CheckEntry(L, host->casts, "Missile");
    const lua_Integer key = IntegerField(L, entry, "seq") + 1;
    SetInteger(L, entry, "seq", key);
    if (lua_getfield(L, entry, "missiles") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, entry, "missiles");
    }
    lua_createtable(L, 1, 0);
    lua_pushvalue(L, 7);
    lua_rawseti(L, -2, 1);
    lua_rawseti(L, -2, key);

    const itemdb::SpellRecord* record = spellfx::Prototype(&effect);
    EffectArrow* arrow =
        effect.NewArrow(from, target, particleType, size, sound, record->lightColor, record->lightRange, launcher);
    if (!arrow) return 0;
    effect.Own(arrow, 0, static_cast<int>(key));
    return PushHandle(L, arrow);
}

// cast:Effect(name [, {at, attach, size, alpha, param, velocity, target, unit, direction, lifetime, free}]): a particle
// effect (a data effect of Res\effects, or a game's type by the scripts' name: Sparks, LightningBlast, ...) at `at`
// (default the cast's point; attached to the object `attach`, at its own place), size, alpha and param (its source's
// parameter: the fire blast's spread) 1, moving by `velocity` ({x, y, z} a tick) towards `target` (a point: where a
// flying one, the fire ball, ends), started on the clients at the unit `unit`'s hand, a wall's (Tendril, Link) extent
// `direction` ({x, y, z}: from its middle to an end); it lasts `lifetime` ticks, else as long as the cast (or as its
// type's own life, when shorter). A `free` one is not the cast's: it lives its type's own life (a one-shot's) past the
// cast's end. Its handle.
int ScriptSpellHost::Effect(lua_State* L) {
    SpellEffect& effect = Self(L, "Effect");
    const int particleType = CheckParticleType(L, 2, "Effect");
    CheckOptions(L, 3);
    lua_settop(L, 9);
    FieldTo(L, 3, "at", 4);
    FieldTo(L, 3, "attach", 5);
    FieldTo(L, 3, "velocity", 6);
    FieldTo(L, 3, "target", 7);
    FieldTo(L, 3, "unit", 8);
    FieldTo(L, 3, "direction", 9);
    MapObject* attach = OptObject(L, 5);
    Vector3 at = attach ? spellfx::kZeroVector : effect.targetPoint;
    MapObject* atObject = nullptr;
    OptPosition(L, 4, &at, &atObject);
    Vector3 velocity = spellfx::kZeroVector;
    MapObject* velocityObject = nullptr;
    OptPosition(L, 6, &velocity, &velocityObject);
    Vector3 target;
    MapObject* targetObject = nullptr;
    const bool aims = OptPosition(L, 7, &target, &targetObject);
    const Unit* unit = UnitOrNull(OptObject(L, 8));
    Vector3 direction;
    MapObject* directionObject = nullptr;
    const bool walls = OptPosition(L, 9, &direction, &directionObject);
    if (walls && !SpellEffect::WallParticle(particleType)) luaL_error(L, "Effect: a direction only for a wall effect");
    const float size = FloatField(L, 3, "size", 1.0f);
    const float alpha = FloatField(L, 3, "alpha", 1.0f);
    const float param = FloatField(L, 3, "param", 1.0f);
    const int lifetime = std::max(IntField(L, 3, "lifetime", 0), 0);
    const bool free = BoolField(L, 3, "free", false);
    EffectParticle* particle = effect.NewParticle(particleType, at, size, alpha, velocity, attach);
    particle->typeParam = param;
    if (aims) particle->target = target;
    if (unit) particle->unitId = unit->id;
    if (walls) static_cast<EffectWall*>(particle)->SetDirection(direction);
    if (free) effect.world->AddTimedObject(particle);
    else effect.Own(particle, lifetime, 0);
    return PushHandle(L, particle);
}

// cast:Sound(name, at [, volume, hearing]) or cast:Sound(name, at, {volume, hearing, velocity}): a sound played once
// at the position (volume 10, heard within 50 unless given), moving by `velocity` ({x, y, z} a tick) as it plays.
int ScriptSpellHost::Sound(lua_State* L) {
    SpellEffect& effect = Self(L, "Sound");
    const char* name = luaL_checkstring(L, 2);
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 3, &at, &atObject, "Sound");
    float volume = spellfx::kVolume;
    float hearing = spellfx::kHearing;
    Vector3 velocity = spellfx::kZeroVector;
    if (lua_istable(L, 4)) {
        volume = FloatField(L, 4, "volume", volume);
        hearing = FloatField(L, 4, "hearing", hearing);
        lua_settop(L, 5);
        FieldTo(L, 4, "velocity", 5);
        MapObject* velocityObject = nullptr;
        OptPosition(L, 5, &velocity, &velocityObject);
    } else {
        volume = scriptlua::FromLuaNumber(luaL_optnumber(L, 4, volume));
        hearing = scriptlua::FromLuaNumber(luaL_optnumber(L, 5, hearing));
    }
    effect.CreateSound(name, at, velocity, volume, hearing);
    return 0;
}

// cast:SoundLoop(at, loop [, {first, volume, hearing, velocity, lifetime}]): sounds at the position for as long as the
// cast holds them: `first` once (none unless given), then `loop` (a name, or a list of names played in random order)
// over and over, at volume 10, heard within 50 unless given, moving by `velocity` ({x, y, z} a tick); they last
// `lifetime` ticks, else as long as the cast. Its handle (cast:Release stops them).
int ScriptSpellHost::SoundLoop(lua_State* L) {
    SpellEffect& effect = Self(L, "SoundLoop");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "SoundLoop");
    std::vector<std::string> names;
    if (lua_istable(L, 3)) {
        const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, 3));
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(L, 3, i);
            const char* name = lua_tostring(L, -1);
            if (!name) return luaL_error(L, "SoundLoop: the loop's names must be strings");
            names.emplace_back(name);
            lua_pop(L, 1);
        }
    } else {
        names.emplace_back(luaL_checkstring(L, 3));
    }
    CheckOptions(L, 4);
    lua_settop(L, 6);
    FieldTo(L, 4, "first", 5);
    FieldTo(L, 4, "velocity", 6);
    const char* first = lua_isnil(L, 5) ? "" : luaL_checkstring(L, 5);
    Vector3 velocity = spellfx::kZeroVector;
    MapObject* velocityObject = nullptr;
    OptPosition(L, 6, &velocity, &velocityObject);
    const float volume = FloatField(L, 4, "volume", spellfx::kVolume);
    const float hearing = FloatField(L, 4, "hearing", spellfx::kHearing);
    const int lifetime = std::max(IntField(L, 4, "lifetime", 0), 0);
    EffectSoundSeq* sounds = effect.NewSoundSequence(at, velocity, first, std::move(names));
    sounds->volume = volume;
    sounds->radius = hearing;
    effect.Own(sounds, lifetime, 0);
    return PushHandle(L, sounds);
}

// cast:Light(at [, {color, range, rise, velocity, lifetime}]): a point light at the position, in the spell record's
// colour ({r, g, b}) and range unless given, rising by `rise` a tick (or moving by `velocity`, {x, y, z} a tick); it
// lasts `lifetime` ticks, else as long as the cast. Its handle.
int ScriptSpellHost::Light(lua_State* L) {
    SpellEffect& effect = Self(L, "Light");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "Light");
    CheckOptions(L, 3);
    const itemdb::SpellRecord* record = spellfx::Prototype(&effect);
    Vector3 color = record->lightColor;
    lua_settop(L, 4);
    FieldTo(L, 3, "color", 4);
    if (!lua_isnil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        color = {FloatField(L, 4, nullptr, 0.0f, 1), FloatField(L, 4, nullptr, 0.0f, 2), FloatField(L, 4, nullptr, 0.0f, 3)};
    }
    const float range = FloatField(L, 3, "range", record->lightRange);
    Vector3 velocity{0.0f, 0.0f, FloatField(L, 3, "rise", 0.0f)};
    lua_settop(L, 5);
    FieldTo(L, 3, "velocity", 5);
    MapObject* velocityObject = nullptr;
    OptPosition(L, 5, &velocity, &velocityObject);
    const int lifetime = std::max(IntField(L, 3, "lifetime", 0), 0);
    EffectPointLight* light = effect.NewPointLight(at, color, range, velocity);
    effect.Own(light, lifetime, 0);
    return PushHandle(L, light);
}

// cast:SetVelocity(handle, velocity): the cast's object of that handle (Bolt, Missile, Effect, Light) moves by
// `velocity` ({x, y, z} a tick) from now on, as the game's spells send their lights up when they fade. False when the
// cast holds no such object (it went).
int ScriptSpellHost::SetVelocity(lua_State* L) {
    SpellEffect& effect = Self(L, "SetVelocity");
    const lua_Integer handle = luaL_checkinteger(L, 2);
    Vector3 velocity;
    MapObject* velocityObject = nullptr;
    CheckPosition(L, 3, &velocity, &velocityObject, "SetVelocity");
    for (const SpellOwned& entry : effect.owned) {
        if (entry.object->id != handle) continue;
        entry.object->SetVelocity(velocity);
        lua_pushboolean(L, true);
        return 1;
    }
    lua_pushboolean(L, false);
    return 1;
}

// cast:Eye(object): the point {x, y, z} the object's spells start from, where the game's lightning puts its light
// (MapObject::EyePoint).
int ScriptSpellHost::Eye(lua_State* L) {
    Self(L, "Eye");
    MapObject* object = OptObject(L, 2);
    if (!object) return luaL_error(L, "Eye: an object expected, got nil or one that is gone");
    PushPoint(L, object->EyePoint());
    return 1;
}

// cast:HitPoint(object): the point {x, y, z} where a hit on a unit shows, as the game's missiles splash: the ground
// below it plus its height in its stance (lower crawling, lying or dead: ask after the hit). Another object's position.
int ScriptSpellHost::HitPoint(lua_State* L) {
    SpellEffect& effect = Self(L, "HitPoint");
    MapObject* object = OptObject(L, 2);
    if (!object) return luaL_error(L, "HitPoint: an object expected, got nil or one that is gone");
    Vector3 at = object->localPosition;
    if (Unit* unit = UnitOrNull(object)) at.z = effect.world->GridHeightAt(at.x, at.y) + unit->ModeSpeed();
    PushPoint(L, at);
    return 1;
}

// cast:IsUnit(object): the object is a unit (false for nil, one that is gone, or another object).
int ScriptSpellHost::IsUnit(lua_State* L) {
    Self(L, "IsUnit");
    lua_pushboolean(L, spellfx::IsUnit(OptObject(L, 2)));
    return 1;
}

// cast:Ground(at [, {above, water}]): the point {x, y, z} on the ground at the position's x and y, `above` higher (a
// float sum: exact); `water`: on the water's surface where there is water (the AI map's terrain and its depth), as
// the game's area spells aim, else the height grid's ground.
int ScriptSpellHost::Ground(lua_State* L) {
    SpellEffect& effect = Self(L, "Ground");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "Ground");
    CheckOptions(L, 3);
    const float above = FloatField(L, 3, "above", 0.0f);
    const bool water = BoolField(L, 3, "water", false);
    const float height = water ? effect.world->RawSurfaceHeightAt(at.x, at.y) : effect.world->GridHeightAt(at.x, at.y);
    at.z = height + above;
    PushPoint(L, at);
    return 1;
}

// cast:Flight(from, to [, speed]): a flight along the ground (the heights left out) from one position to the other
// at `speed` a second (default the spell's; 5 when it is not above 0): {ticks, distance, speed, velocity}, the ticks
// it takes (rounded), the ground distance, the speed taken and the velocity a tick ({x, y, 0}), as the fireball flies.
int ScriptSpellHost::Flight(lua_State* L) {
    SpellEffect& effect = Self(L, "Flight");
    Vector3 from, to;
    MapObject* object = nullptr;
    CheckPosition(L, 2, &from, &object, "Flight");
    CheckPosition(L, 3, &to, &object, "Flight");
    float speed = lua_isnoneornil(L, 4) ? effect.HeldSpell()->speed : scriptlua::FromLuaNumber(luaL_checknumber(L, 4));
    Vector3 velocity = to - from;
    velocity.z = 0.0f;
    const float distance = Length2D(XY(velocity));
    speed = speed > 0.0f ? speed : spellfx::kDefaultSpeed;
    const int ticks = spellfx::FlightTicks(distance, speed);
    velocity = spellfx::ScaledToSpeed(velocity, speed);
    lua_createtable(L, 0, 4);
    const int table = lua_gettop(L);
    SetInteger(L, table, "ticks", ticks);
    SetNumber(L, table, "distance", distance);
    SetNumber(L, table, "speed", speed);
    PushPoint(L, velocity);
    lua_setfield(L, table, "velocity");
    return 1;
}

// cast:Release(handle): the cast's object of that handle (Bolt, Missile, Effect, Light, SoundLoop) leaves the world
// now, as the game's spells drop theirs; a missile's OnHit never runs. False when the cast holds no such object (nil
// included).
int ScriptSpellHost::Release(lua_State* L) {
    SpellEffect& effect = Self(L, "Release");
    if (lua_isnoneornil(L, 2)) {
        lua_pushboolean(L, false);
        return 1;
    }
    const lua_Integer handle = luaL_checkinteger(L, 2);
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int entry = CheckEntry(L, host->casts, "Release");
    const auto found =
        std::ranges::find_if(effect.owned, [handle](const SpellOwned& owned) { return owned.object->id == handle; });
    if (found == effect.owned.end()) {
        lua_pushboolean(L, false);
        return 1;
    }
    if (found->payload && lua_getfield(L, entry, "missiles") == LUA_TTABLE) {
        lua_pushnil(L);
        lua_rawseti(L, -2, found->payload);
    }
    effect.world->RemoveTimedObject(static_cast<unsigned>(found->object->id));
    effect.owned.erase(found);
    lua_pushboolean(L, true);
    return 1;
}

// cast:Noise(at, loudness [, delay]): the caster makes a noise at the position, as the fireball's blast does: the
// hostile units within 50 of it whose hearing x loudness reaches them come to look where the caster stands, with the
// AI's call delay `delay` (default 26, a unit's own noises'). Nothing without a unit caster.
int ScriptSpellHost::Noise(lua_State* L) {
    SpellEffect& effect = Self(L, "Noise");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "Noise");
    const float loudness = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    const lua_Integer delay = std::clamp<lua_Integer>(luaL_optinteger(L, 4, AIBrain::kCallDelay), 0, INT_MAX);
    if (spellfx::IsUnit(effect.caster))
        effect.world->aiMap.events.Noise(spellfx::ControllerOf(effect.caster), loudness, static_cast<int>(delay), at);
    return 0;
}

// cast:Across(from, to, length): the vector {x, y, 0} `length` long square to the ground line from `from` to `to` (to
// its left), as the game's walls stand across the line from their caster to their point; along x when the two have
// the same x and y.
int ScriptSpellHost::Across(lua_State* L) {
    Self(L, "Across");
    Vector3 from, to;
    MapObject* object = nullptr;
    CheckPosition(L, 2, &from, &object, "Across");
    CheckPosition(L, 3, &to, &object, "Across");
    const float length = scriptlua::FromLuaNumber(luaL_checknumber(L, 4));
    PushPoint(L, spellfx::WallSpan(spellfx::WallDirection(to, from), length));
    return 1;
}

// cast:Block(at, {along} or {radius}): the AI map's cells under a wall from at - along to at + along (a half width of
// 0.5), or a disc of `radius` around `at`, count as a spell's (units path around them, as around the game's walls and
// clouds) until cast:Unblock() or the cast's end; a second Block replaces the first. It also works in OnResume (a
// resumed wall blocks again, as the game's load did).
int ScriptSpellHost::Block(lua_State* L) {
    SpellEffect& effect = Self(L, "Block", true);
    SpellFootprint footprint;
    MapObject* object = nullptr;
    CheckPosition(L, 2, &footprint.at, &object, "Block");
    CheckOptions(L, 3);
    lua_settop(L, 4);
    FieldTo(L, 3, "along", 4);
    footprint.disc = !OptPosition(L, 4, &footprint.along, &object);
    if (footprint.disc) {
        lua_getfield(L, 3, "radius");
        if (!lua_isnumber(L, -1)) luaL_error(L, "Block: along {x, y, z} or a radius expected");
        lua_pop(L, 1);
        footprint.radius = FloatField(L, 3, "radius", 0.0f);
    }
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int entry = CheckEntry(L, host->casts, "Block");
    PushFootprint(L, footprint);
    lua_setfield(L, entry, "block");
    effect.Block(footprint);
    return 0;
}

// cast:Unblock(): the cells cast:Block blocked are free again; false when it blocks none.
int ScriptSpellHost::Unblock(lua_State* L) {
    SpellEffect& effect = Self(L, "Unblock");
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int entry = CheckEntry(L, host->casts, "Unblock");
    lua_pushnil(L);
    lua_setfield(L, entry, "block");
    const bool blocked = effect.footprintStamped != 0;
    if (blocked) effect.Unblock();
    lua_pushboolean(L, blocked);
    return 1;
}

// cast:Position(object): the point {x, y, z} where the object is (its own position, not the ground below it), where
// the teleport's departure shows.
int ScriptSpellHost::Position(lua_State* L) {
    Self(L, "Position");
    MapObject* object = OptObject(L, 2);
    if (!object) return luaL_error(L, "Position: an object expected, got nil or one that is gone");
    PushPoint(L, object->localPosition);
    return 1;
}

// cast:IsDead(unit): the unit is dead, as the game's spells test their units (false for nil, one that is gone, or
// another object).
int ScriptSpellHost::IsDead(lua_State* L) {
    Self(L, "IsDead");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    lua_pushboolean(L, unit && unit->IsDead());
    return 1;
}

// cast:IsHurt(unit): the unit's hit points as the players see them (whole numbers) are not its maximum, as the healing
// spell tests it for its sound and light (and the items for their healing); false for nil or another object.
int ScriptSpellHost::IsHurt(lua_State* L) {
    Self(L, "IsHurt");
    const Unit* unit = UnitOrNull(OptObject(L, 2));
    lua_pushboolean(L, unit && unit->packedVitals.hitPoints != unit->packedVitals.maxHitPoints);
    return 1;
}

// cast:Shout(unit, ack): the unit says the ack to its player (acks.db's kinds by their TOML names: "cant_teleport",
// "link_ended", ...), as the teleport's caster says it cannot go; nothing for a unit without a player or a kind units
// do not shout.
int ScriptSpellHost::Shout(lua_State* L) {
    Self(L, "Shout");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    const char* name = luaL_checkstring(L, 3);
    const std::optional<AckKind> kind = acks::KindByName(name);
    if (!kind) return luaL_error(L, "Shout: no ack %s", name);
    if (unit) spellfx::ControllerOf(unit)->Shout(*kind);
    return 0;
}

// cast:Reveal(unit): the caster's player sees the unit this tick (its party's view takes it in), as clairvoyance shows
// the units in its radius; nothing without a unit caster that has a player.
int ScriptSpellHost::Reveal(lua_State* L) {
    SpellEffect& effect = Self(L, "Reveal");
    MapObject* object = OptObject(L, 2);
    Unit* caster = effect.caster;
    if (object && caster && caster->player) caster->player->Server()->possessedGroup.Insert(object);
    return 0;
}

// cast:Wake(at, radius): the units in the map's sectors within `radius` of the position are updated again (sleeping
// ones wake), as fireworks and clairvoyance wake those around them.
int ScriptSpellHost::Wake(lua_State* L) {
    SpellEffect& effect = Self(L, "Wake");
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 2, &at, &atObject, "Wake");
    const float radius = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    effect.world->WakeUnitsAround(at, radius);
    return 0;
}

// cast:Notice(unit, at, level): the unit's AI notices the position at `level` (a whole number) when that is above the
// level of what it noticed last, as fireworks frighten the units in their radius (300 falling to 50 over their
// duration); nothing for another object.
int ScriptSpellHost::Notice(lua_State* L) {
    Self(L, "Notice");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 3, &at, &atObject, "Notice");
    const lua_Integer level = std::clamp<lua_Integer>(luaL_checkinteger(L, 4), INT_MIN, INT_MAX);
    if (unit) spellfx::ControllerOf(unit)->brain.memory.Notice(at, static_cast<int>(level), kNoticeDecay);
    return 0;
}

// cast:Vision(seer, observer [, {lifetime}]): what the unit `seer` sees, with the unit `observer`'s senses, shown in the
// fog of war while it lasts (`lifetime` ticks, else as long as the cast), as vision fog shows its caster what its
// target sees. Its handle.
int ScriptSpellHost::Vision(lua_State* L) {
    SpellEffect& effect = Self(L, "Vision");
    const Unit* seer = UnitOrNull(OptObject(L, 2));
    const Unit* observer = UnitOrNull(OptObject(L, 3));
    if (!seer || !observer) return luaL_error(L, "Vision: two units expected (the seer, the observer)");
    CheckOptions(L, 4);
    const int lifetime = std::max(IntField(L, 4, "lifetime", 0), 0);
    auto* shown = spellfx::CreateObject<EffectShowVisibility>(&effect, ClassId::EffectShowVisibility);
    shown->SetUnits(observer->id, seer->id);
    spellfx::SetPosition(shown, spellfx::kZeroVector);
    effect.Own(shown, lifetime, 0);
    return PushHandle(L, shown);
}

int ScriptSpellHost::PushModule(const SpellEffect& effect) {
    const auto clock = clocks.find(effect.castKey);
    if (clock == clocks.end()) return PushModule(ScriptOf(effect));
    int& module = clock->second.module;
    if (module == Clock::kUnresolved) return module = PushModule(ScriptOf(effect));
    if (module == LUA_NOREF) lua_pushnil(L);
    else lua_rawgeti(L, LUA_REGISTRYINDEX, module);
    return module;
}

int ScriptSpellHost::PushModule(const std::string& file) {
    std::string key = file;
    text::LowerInPlace(key.data(), key.size());
    if (const auto found = modules.find(key); found != modules.end()) {
        if (found->second == LUA_NOREF) lua_pushnil(L);
        else lua_rawgeti(L, LUA_REGISTRYINDEX, found->second);
        return found->second;
    }

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
        modules[key] = LUA_NOREF;
        lua_pushnil(L);
        return LUA_NOREF;
    }
    lua_pushvalue(L, -1);
    const int module = luaL_ref(L, LUA_REGISTRYINDEX);
    modules[key] = module;
    return module;
}

void ScriptSpellHost::PushEntry(lua_State* state, int key) {
    lua_rawgeti(state, LUA_REGISTRYINDEX, casts);
    lua_rawgeti(state, -1, key);
    lua_remove(state, -2);
}

void ScriptSpellHost::PushCast(const SpellEffect& effect, int entry) {
    const auto found = clocks.find(effect.castKey);
    Clock* const clock = found != clocks.end() ? &found->second : nullptr;
    const bool fresh = !clock || clock->value == LUA_NOREF;
    CastFields unkept;
    CastFields& shown = clock ? clock->shown : unkept;
    if (fresh) lua_createtable(L, 0, 24);
    else lua_rawgeti(L, LUA_REGISTRYINDEX, clock->value);
    const int cast = lua_gettop(L);
    const Spell& spell = *effect.HeldSpell();
    const itemdb::SpellRecord* record = spell.GetPrototype();
    const bool newRecord = fresh || record != shown.record;
    // The fields in the order a fresh table gets them; a kept one's only when they changed (floats by their bits).
    const auto number = [&](const char* name, int slot, float value) {
        float& was = shown.numbers[slot];
        if (!fresh && std::bit_cast<std::uint32_t>(was) == std::bit_cast<std::uint32_t>(value)) return;
        was = value;
        SetNumber(L, cast, name, value);
    };
    const auto integer = [&](const char* name, auto& was, auto value) {
        if (!fresh && was == value) return;
        was = value;
        SetInteger(L, cast, name, value);
    };
    const auto object = [&](const char* name, MapObject*& was, MapObject* value) {
        if (!fresh && was == value) return;
        was = value;
        scriptlua::PushObject(L, value);
        lua_setfield(L, cast, name);
    };
    const auto point = [&](const char* name, Vector3& was, const Vector3& value) {
        const auto bits = [](const Vector3& v) {
            return std::array{std::bit_cast<std::uint32_t>(v.x), std::bit_cast<std::uint32_t>(v.y),
                              std::bit_cast<std::uint32_t>(v.z)};
        };
        if (!fresh && bits(was) == bits(value)) return;
        was = value;
        PushPoint(L, value);
        lua_setfield(L, cast, name);
    };
    if (fresh) SetInteger(L, cast, "id", effect.castKey);
    if (newRecord) {
        lua_pushstring(L, record ? record->code.c_str() : "");
        lua_setfield(L, cast, "code");
    }
    object("caster", shown.caster, effect.caster.Get());
    object("target", shown.target, effect.target.Get());
    point("from", shown.from, effect.position);
    point("point", shown.point, effect.targetPoint);
    integer("tick", shown.tick, IntegerField(L, entry, "tick"));
    number("damage", 0, spell.damage);
    number("range", 1, spell.range);
    number("radius", 2, spell.radius);
    integer("duration", shown.duration, spell.duration);
    integer("targetCount", shown.targetCount, spell.targetCount);
    number("speed", 3, spell.speed);
    number("mana", 4, spell.manaCost);
    number("power", 5, spellfx::Power(&effect));
    number("areaPower", 6, record ? spellfx::AreaCurve(spell.radius, record->area) : 0.0f);
    number("lightRise", 7, record ? spellfx::LightRise(&effect) : 0.0f);
    number("lightRange", 8, record ? record->lightRange : 0.0f);
    if (newRecord) {
        shown.record = record;
        const auto school = static_cast<std::size_t>(record ? record->school : SpellSchool::None);
        lua_pushstring(L,
                       school < std::size(recordnames::kSpellSchools) ? recordnames::kSpellSchools[school].data() : "");
        lua_setfield(L, cast, "school");
    }
    if (fresh || shown.silent != (effect.silentEnd != 0)) {
        shown.silent = effect.silentEnd != 0;
        lua_pushboolean(L, shown.silent);
        lua_setfield(L, cast, "silent");
    }
    lua_getfield(L, entry, "data");
    lua_setfield(L, cast, "data");
    if (!fresh) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, castMeta);
    lua_setmetatable(L, cast);
    if (!clock) return;
    lua_pushvalue(L, cast);
    clock->value = luaL_ref(L, LUA_REGISTRYINDEX);
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
    // A known clock (a tick's hooks, the entry's tick written first) holds the tick and the earliest due already, and
    // the cast is not finished (Finish makes the clock unknown); else they are read from the entry.
    const int base = lua_gettop(L);
    const auto known = clocks.find(effect.castKey);
    bool finished = false;
    lua_Integer tick = 0, due = 0;
    if (known != clocks.end() && known->second.known) {
        tick = known->second.tick;
        due = known->second.due;
    } else {
        PushEntry(L, effect.castKey);
        const int entry = base + 1;
        if (!lua_istable(L, entry)) {
            lua_settop(L, base);
            Drop(effect);
            return false;
        }
        finished = EntryFlag(entry, "finished");
        tick = IntegerField(L, entry, "tick");
        // The earliest timer's due tick for the clock (0: no timer; one at 0 or below is due at the next tick).
        if (lua_getfield(L, entry, "timers") == LUA_TTABLE) {
            const auto count = static_cast<lua_Integer>(lua_rawlen(L, -1));
            for (lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, i);
                const lua_Integer at = IntegerField(L, -1, "due");
                lua_pop(L, 1);
                if (i == 1 || at < due) due = at;
            }
            if (count > 0 && due <= 0) due = tick + 1;
        }
        lua_settop(L, base);
    }
    const bool pending = due != 0 || effect.OwnsPending();
    PushModule(effect);
    const bool ticks = lua_istable(L, -1) && lua_getfield(L, -1, "OnTick") == LUA_TFUNCTION;
    lua_settop(L, base);
    const lua_Integer cap = TickCap(effect, ticks);

    const char* reason = nullptr;
    if (finished) {
        reason = "finished";
    } else if (tick >= cap) {
        reason = "limit";
    } else if (!pending && !ticks) {
        reason = "idle";
    }
    if (!reason) {
        Clock& clock = clocks[effect.castKey];
        clock.tick = tick;
        clock.due = due;
        clock.known = true;
        clock.stale = false;
        return true;
    }
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
    if (const auto clock = clocks.find(effect.castKey); clock != clocks.end()) {
        ReleaseValue(clock->second);
        clocks.erase(clock);
    }
    effect.host = nullptr;
}

bool ScriptSpellHost::EntryFlag(int entry, const char* name) {
    lua_getfield(L, entry, name);
    const bool flag = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return flag;
}

bool ScriptSpellHost::Finished(const SpellEffect& effect, const Clock& clock) {
    if (clock.known) return false;
    PushEntry(L, effect.castKey);
    const bool finished = lua_istable(L, -1) && EntryFlag(lua_gettop(L), "finished");
    lua_pop(L, 1);
    return finished;
}

void ScriptSpellHost::Report(const std::string& message) {
    if (scheduler.report) scheduler.report(message);
}
