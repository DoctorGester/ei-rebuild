// The scripted spells' runtime in the zone's server Lua state. See script_spell_host.h and docs/notes/items_spells.md,
// "Scripted spells".
#include "game/script/script_spell_host.h"

#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_scheduler.h"
#include "game/data/record_names.h"
#include "game/spells/spell.h"
#include "game/spells/spell_effect_internal.h"
#include "game/units/unit.h"

#include "lib/text_case.h"
#include "platform/platform.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace {

constexpr const char* kFolder = "Res\\spells";
// The ticks a cast lives at most when its spell's duration does not say (Settle).
constexpr lua_Integer kDefaultTickCap = 600;
// The longest delay a timer takes, in ticks.
constexpr double kMaxDelay = 1e9;

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
    lua_createtable(L, 0, 15);
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

    // The owned objects' lives, then OnHit(cast, target, payload, point) for each missile that arrived, in order.
    std::vector<SpellArrival> arrived;
    effect.TickOwned(&arrived);
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
        if (result == Hook::Failed) return Settle(effect, result);
        PushEntry(L, effect.castKey);
        const bool finished = EntryFlag(lua_gettop(L), "finished");
        lua_settop(L, base);
        if (finished) return Settle(effect, result);
    }

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

SpellEffect& ScriptSpellHost::Self(lua_State* L, const char* method) {
    auto* host = static_cast<ScriptSpellHost*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (host->scheduler.lua.worldOff) luaL_error(L, "%s: no bindings at a spell file's top level", method);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "id");
    const lua_Integer key = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    const auto found = host->live.find(static_cast<int>(key));
    if (found == host->live.end() || !found->second->world) luaL_error(L, "%s: the cast has ended", method);
    return *found->second;
}

// cast:FindUnits(at, radius [, filter]): the units within `radius` of `at`, nearest first (then by id), that pass
// every key the filter sets: enemies / friends (of the caster: either regards the other's side as hostile; with no
// caster every unit is an enemy), alive, area (those area spells hit), sight (the line of sight the spell needs, from
// `at`), affect (the spell's target flags let it hit them), exclude (a set of ids: {[id] = true}).
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
    lua_settop(L, 4);
    lua_getfield(L, 4, "exclude");
    const bool excludes = lua_istable(L, 5);

    lua_newtable(L);
    const int result = lua_gettop(L);
    Unit* const caster = effect.caster;
    std::vector<Unit*> units = effect.world->aiMap.sectorMap.FindUnitsInRadius(at, radius);
    std::erase_if(units, [&](Unit* unit) {
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
            lua_rawgeti(L, 5, unit->id);
            const bool excluded = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            if (excluded) return true;
        }
        return false;
    });
    std::ranges::sort(units, [&at](const Unit* a, const Unit* b) {
        const float da = LengthSquared(a->localPosition - at), db = LengthSquared(b->localPosition - at);
        if (da != db) return da < db;
        return a->id < b->id;
    });
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

// cast:Damage(unit, amount, type [, {area, noCredit}]): a spell's hit on a unit (SpellEffect::HitUnit: only what the
// spell may affect; the caster has the credit unless noCredit), `type` a damage type's name, `area` a hit without a
// direction (the walls' and clouds'). The hit points the unit lost; another object is not hit, as by the game's
// spells.
int ScriptSpellHost::Damage(lua_State* L) {
    SpellEffect& effect = Self(L, "Damage");
    Unit* unit = UnitOrNull(OptObject(L, 2));
    const float amount = scriptlua::FromLuaNumber(luaL_checknumber(L, 3));
    const DamageType type = CheckDamageType(L, 4, "Damage");
    CheckOptions(L, 5);
    const int hitKind = BoolField(L, 5, "area", false) ? 1 : 0;
    const bool noCredit = BoolField(L, 5, "noCredit", false);
    float lost = 0.0f;
    if (unit) {
        DamageInfo hit = SpellEffect::TypedHit(type, amount, hitKind);
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

// cast:Effect(name [, {at, attach, size, alpha, velocity, lifetime}]): a particle effect (a data effect of
// Res\effects, or a game's type by the scripts' name: Sparks, LightningBlast, ...) at `at` (default the cast's point;
// attached to the object `attach`, at its own place), size and alpha 1, moving by `velocity` ({x, y, z} a tick); it
// lasts `lifetime` ticks, else as long as the cast (or as its type's own life, when shorter). Its handle.
int ScriptSpellHost::Effect(lua_State* L) {
    SpellEffect& effect = Self(L, "Effect");
    const int particleType = CheckParticleType(L, 2, "Effect");
    CheckOptions(L, 3);
    lua_settop(L, 6);
    FieldTo(L, 3, "at", 4);
    FieldTo(L, 3, "attach", 5);
    FieldTo(L, 3, "velocity", 6);
    MapObject* attach = OptObject(L, 5);
    Vector3 at = attach ? spellfx::kZeroVector : effect.targetPoint;
    MapObject* atObject = nullptr;
    OptPosition(L, 4, &at, &atObject);
    Vector3 velocity = spellfx::kZeroVector;
    MapObject* velocityObject = nullptr;
    OptPosition(L, 6, &velocity, &velocityObject);
    const float size = FloatField(L, 3, "size", 1.0f);
    const float alpha = FloatField(L, 3, "alpha", 1.0f);
    const int lifetime = std::max(IntField(L, 3, "lifetime", 0), 0);
    EffectParticle* particle = effect.NewParticle(particleType, at, size, alpha, velocity, attach);
    effect.Own(particle, lifetime, 0);
    return PushHandle(L, particle);
}

// cast:Sound(name, at [, volume, hearing]): a sound played once at the position (volume 10, heard within 50 unless
// given).
int ScriptSpellHost::Sound(lua_State* L) {
    SpellEffect& effect = Self(L, "Sound");
    const char* name = luaL_checkstring(L, 2);
    Vector3 at;
    MapObject* atObject = nullptr;
    CheckPosition(L, 3, &at, &atObject, "Sound");
    const float volume = scriptlua::FromLuaNumber(luaL_optnumber(L, 4, spellfx::kVolume));
    const float hearing = scriptlua::FromLuaNumber(luaL_optnumber(L, 5, spellfx::kHearing));
    effect.CreateSound(name, at, spellfx::kZeroVector, volume, hearing);
    return 0;
}

// cast:Light(at [, {color, range, rise, lifetime}]): a point light at the position, in the spell record's colour
// ({r, g, b}) and range unless given, rising by `rise` a tick; it lasts `lifetime` ticks, else as long as the cast.
// Its handle.
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
    const Vector3 rise{0.0f, 0.0f, FloatField(L, 3, "rise", 0.0f)};
    const int lifetime = std::max(IntField(L, 3, "lifetime", 0), 0);
    EffectPointLight* light = effect.NewPointLight(at, color, range, rise);
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
    SetNumber(L, cast, "power", spellfx::Power(&effect));
    SetNumber(L, cast, "lightRise", record ? spellfx::LightRise(&effect) : 0.0f);
    const auto school = static_cast<std::size_t>(record ? record->school : SpellSchool::None);
    lua_pushstring(L, school < std::size(recordnames::kSpellSchools) ? recordnames::kSpellSchools[school].data() : "");
    lua_setfield(L, cast, "school");
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
    const bool pending = timers || effect.OwnsPending();
    PushModule(effect);
    const bool ticks = lua_istable(L, -1) && lua_getfield(L, -1, "OnTick") == LUA_TFUNCTION;
    lua_settop(L, entry - 1);
    // A cast with OnTick lives at most its spell's duration; one paced by its timers alone at most kDefaultTickCap
    // ticks, or its duration when that is longer (the game's spells' durations are target counts or the ticks of
    // what they leave behind, which their timers outlast).
    const auto duration = static_cast<lua_Integer>(effect.HeldSpell()->duration);
    const lua_Integer cap = ticks ? (duration > 0 ? duration : kDefaultTickCap) : std::max(duration, kDefaultTickCap);

    const char* reason = nullptr;
    if (finished) {
        reason = "finished";
    } else if (tick >= cap) {
        reason = "limit";
    } else if (!pending && !ticks) {
        reason = "idle";
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
