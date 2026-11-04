// Evaluation of script functions: ScriptCall::CalcParameters, CalcFloat, CalcObject and CalcGroup. Rebirth's functions
// (script_addon.h) are tried first.
// Floating point: the comparisons are plain (a NaN argument compares as IEEE says, where the
// old x87 compare counted it as equal and less).
#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"
#include "game_singletons.h"
#include "lever.h"
#include "map_object.h"
#include "net_classes.h"
#include "object.h"
#include "object_diplomacy.h"
#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "script_addon.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "camera.h"

#include "lib/rounding.h"
#include "lib/str.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

namespace {

// ---- Conversions --------------------------------------------------------------------------------

float Distance(Vector2 a, Vector2 b) { return Length2D(a - b); }

// As IsInArea tests circles.
float SquaredDistance(float x, float y, float centreX, float centreY) {
    return DistanceSquared2D(Vector2{x, y}, Vector2{centreX, centreY});
}

// value * range / 2^32.
float ScaleRandom(std::uint32_t value, float range) { return range * (value * 0x1p-32f); }

// (float)(time + add) rounded to nearest: the sum (the int loaded whole) is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    return lib::RoundEven(static_cast<float>(time) + extra);
}

constexpr float kMinusOne = -1.0f;
constexpr float kFar = 1e30f;     // the distance when an object is missing

// ---- Game state reached by the functions --------------------------------------------------------



WorldAlarm& Alarm(int index) { return g_serverWorld.alarms[index]; }

// GetWorldTime: (time * scale + offset) mod 24 (the tick loaded whole; fmod is exact).
float WorldTimeOfDay() {
    const WorldServer* world = &g_serverWorld;
    return std::fmod((world->secondsPerTick * world->tick) + world->timeBase, 24.0f);
}

// What a unit sees: the AI brain's visible list.
void InsertSeen(ScriptGroup* group, UnitServer* controller) {
    const auto& visible = controller->brain.memory.visible;
    for (auto node = visible.begin(); node != visible.end();) {
        auto current = node;
        node = std::next(current);
        group->Insert(*current);
    }
}

} // namespace

void ScriptCall::PrepareArguments(Script* script, ScriptVars* vars) {
    SetParameters(localBindings, vars);
    SetParameters(globalBindings, &script->globals);
    for (int i = 0; i < std::ssize(subCalls); ++i) CalcParameters(script, vars, &subCalls[i]);
}

const ScriptSubCall* ScriptCall::ConditionCall() const {
    for (const ScriptSubCall& call : subCalls)
        if (call.type == kScriptFloat) return &call;
    return nullptr;
}

void ScriptCall::CalcParameters(Script* script, ScriptVars* vars, const ScriptSubCall* call) {
    switch (call->type) {
    case kScriptFloat:
        floatArgs[call->argIndex] = call->cmd->CalcFloat(script, vars);
        return;
    case kScriptString: {
        // Only Rebirth's functions return strings.
        scriptaddon::CalcString(call->cmd.get(), script, vars, &stringArgs[call->argIndex]);
        return;
    }
    case kScriptObject:
        call->cmd->CalcObject(script, vars, &objectArgs[call->argIndex]);
        return;
    case kScriptGroup:
        call->cmd->CalcGroup(script, vars, &groupArgs[call->argIndex]);
        return;
    default:
        game::FatalError("ScriptCall::CalcParameters, unknown function parameter type");
    }
}

float ScriptCall::CalcFloat(Script* script, ScriptVars* vars) {
    PrepareArguments(script, vars);
    float result = 0.0f;
    {
        scriptaddon::Call call{this, script, &result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return result;
    }
    const float* f = floatArgs.data();
    const ScriptFunction function = static_cast<ScriptFunction>(id);
    switch (function) {
    case ScriptFunction::IsEqual:
        return ScriptBool(f[0] == f[1]);
    case ScriptFunction::IsLess:
        return ScriptBool(f[0] < f[1]);
    case ScriptFunction::IsGreater:
        return ScriptBool(f[0] > f[1]);
    case ScriptFunction::IsEqualString:
        return ScriptBool(str::Compare(stringArgs[0], stringArgs[1]) == 0);
    case ScriptFunction::Add:
        return f[1] + f[0];
    case ScriptFunction::Sub:
        return f[0] - f[1];
    case ScriptFunction::Mul:
        return f[1] * f[0];
    case ScriptFunction::Div:
        return f[0] / f[1];
    case ScriptFunction::Not:
        return ScriptBool(f[0] == 0.0f);

    case ScriptFunction::HP:
    case ScriptFunction::MaxHP:
    case ScriptFunction::Mana:
    case ScriptFunction::MaxMana: {
        MapObject* o = objectArgs[0]->object;
        MapObjectServer* stats = o ? o->ServerCompanion() : nullptr;
        if (stats) {
            const UnitVitals& vitals = stats->vitals;
            const float* value = function == ScriptFunction::HP ? &vitals.hitPoints
                               : function == ScriptFunction::MaxHP ? &vitals.maxHitPoints
                               : function == ScriptFunction::Mana ? &vitals.mana
                                            : &vitals.maxMana;
            result = *value;
        } else {
            result = 0.0f;
        }
        return result;
    }
    case ScriptFunction::GetX:
    case ScriptFunction::GetY: {
        MapObject* o = objectArgs[0]->object;
        if (o) result = function == ScriptFunction::GetX ? o->localPosition.x : o->localPosition.y;
        else result = 0.0f;
        return result;
    }
    case ScriptFunction::GetZ: { // GetZ: height above the ground at the object's position
        MapObject* o = objectArgs[0]->object;
        if (!o) {
            return 0.0f;
        }
        float x = o->localPosition.x, y = o->localPosition.y, z = o->localPosition.z;
        if (Unit* unit = UnitOrNull(o)) result = unit->GroundHeight(x, y) + z;   // (o is not null)
        else result = g_serverWorld.GridHeight(x, y) + z;
        return result;
    }
    case ScriptFunction::GetLootItemsCount:
    case ScriptFunction::GetMoney: {
        Player* player = g_serverWorld.GetPlayer(lib::Truncate(f[0]));
        if (player) result = static_cast<float>(function == ScriptFunction::GetLootItemsCount ? std::ssize(player->items) : player->GetParty()->money);
        return result;
    }
    case ScriptFunction::WasLooted: {
        MapObject* o = objectArgs[0]->object;
        return ScriptBool(!o || lib::Has(o->flags, MapObjectFlag::LootTaken));
    }
    case ScriptFunction::GetFutureX:
    case ScriptFunction::GetFutureY: {
        MapObject* o = objectArgs[0]->object;
        if (!o) {
            return 0.0f;
        }
        Vector3 position = o->PositionAt(RoundTimeOffset(g_serverWorld.tick, f[0]));
        return function == ScriptFunction::GetFutureX ? position.x : position.y;
    }
    case ScriptFunction::IsUnitInWater: { // IsUnitInWater: flag 0x80 of the AI map cell at the object's position
        MapObject* o = objectArgs[0]->object;
        return ScriptBool(o && g_serverWorld.aiMap.InWaterAt(o->localPosition));
    }
    case ScriptFunction::GroupSize:
        return static_cast<float>(groupArgs[0]->size());
    case ScriptFunction::GroupHas: {
        MapObject* o = objectArgs[0]->object;
        return ScriptBool(groupArgs[0]->Contains(o));
    }
    case ScriptFunction::Random: { // Random(range): [0, range]
        float range = f[0];
        std::uint32_t value = g_random.Next();
        return ScaleRandom(value, range);
    }
    case ScriptFunction::GetDiplomacy: { // GetDiplomacy: 1 allied, -1 enemies, 0 otherwise
        int b = lib::Truncate(f[1]);
        int a = lib::Truncate(f[0]);
        int relation = g_serverWorld.diplomacy.GetRelation(a, b);
        if (relation == 2) result = kMinusOne;
        else if (relation == 0) result = 1.0f;
        return result;
    }
    case ScriptFunction::IsAlive:
    case ScriptFunction::IsDead: {
        MapObject* o = objectArgs[0]->object;
        MapObjectCompanion* stats = o ? o->companion.Get() : nullptr;
        if (!stats) return result;
        int state = stats->IsDead();
        return function == ScriptFunction::IsAlive ? ScriptBool(state == 0) : static_cast<float>(state);
    }
    case ScriptFunction::IsEnemy: { // IsEnemy(unit, player)
        Unit* unit = UnitOrNull(objectArgs[0]->object);
        if (!unit) return result;
        int player = lib::Truncate(f[0]);
        std::uint32_t mask = 1u << (player & 31);
        bool enemy = (mask & unit->relationsB & unit->relationsA) != 0;
        return ScriptBool(enemy);
    }
    case ScriptFunction::GetPlayer: {
        Unit* unit = UnitOrNull(objectArgs[0]->object);
        if (unit) result = static_cast<float>(unit->side);
        return result;
    }
    case ScriptFunction::GetAIClass: { // GetAIClass (the unit's size class)
        Unit* unit = UnitOrNull(objectArgs[0]->object);
        result = kMinusOne;
        if (unit) result = static_cast<float>(unit->sizeClass);
        return result;
    }
    case ScriptFunction::GetLeverState: {
        MapObject* o = objectArgs[0]->object;
        if (Lever* lever = LeverOrNull(o)) result = static_cast<float>(lever->state);
        return result;
    }
    case ScriptFunction::GetObjectID: {
        MapObject* o = objectArgs[0]->object;
        if (o) result = static_cast<float>(o->id);
        return result;
    }
    case ScriptFunction::IsAlarm:
        return ScriptBool(Alarm(lib::RoundEven(f[0])).active != 0);
    case ScriptFunction::AlarmTime:
        return static_cast<float>(Alarm(lib::RoundEven(f[0])).time);
    case ScriptFunction::AlarmPosX:
        return Alarm(lib::RoundEven(f[0])).x;
    case ScriptFunction::AlarmPosY:
        return Alarm(lib::RoundEven(f[0])).y;
    case ScriptFunction::DistanceUnitUnit: {
        MapObject* a = objectArgs[0]->object;
        MapObject* b = objectArgs[1]->object;
        if (a && b) result = Distance(XY(a->localPosition), XY(b->localPosition));
        else result = kFar;
        return result;
    }
    case ScriptFunction::DistanceUnitPoint: {
        MapObject* o = objectArgs[0]->object;
        if (o) result = Distance(XY(o->localPosition), Vector2{f[0], f[1]});
        else result = kFar;
        return result;
    }

    // Every/Any/Sum(var, group, condition): the condition (the nested float call) is evaluated
    // again for each member of the group with the object variable set to that member. Without a
    // nested call the condition is a constant.
    case ScriptFunction::Every:
    case ScriptFunction::Any:
    case ScriptFunction::Sum: {
        const ScriptSubCall* condition = ConditionCall();
        if (!condition) {
            if (function == ScriptFunction::Every) result = floatArgs[0];
            else if (function == ScriptFunction::Any) {
                result = ScriptBool(floatArgs[0] != 0.0f);
            }
            else result = static_cast<float>(groupArgs[0]->size()) * floatArgs[0];
            return result;
        }
        result = ScriptBool(function == ScriptFunction::Every);
        ScriptGroup* g = groupArgs[0];
        if (g->empty()) return result;
        int k = 0;
        do {
            *objectArgs[0] = (*g)[static_cast<std::size_t>(k)];
            CalcParameters(script, vars, condition);
            if (function == ScriptFunction::Every) {
                if (floatArgs[0] == 0.0f) {
                    return 0.0f;
                }
            } else if (function == ScriptFunction::Any) {
                if (floatArgs[0] != 0.0f) {
                    return 1.0f;
                }
            } else {
                result = floatArgs[0] + result;
            }
            ++k;
            g = groupArgs[0];
        } while (k < std::ssize(*g));
        return result;
    }

    case ScriptFunction::HaveItem: { // HaveItem(player, item)
        auto* player = g_serverWorld.GetPlayer(lib::Truncate(f[0]));
        return ScriptBool(player && player->HasLoot(lib::Truncate(f[1])));
    }
    case ScriptFunction::IsPlayerInDanger:
    case ScriptFunction::IsPlayerInSafety: {
        Player* player = g_serverWorld.GetPlayer(lib::Truncate(f[0]));
        if (!player) return result;
        PlayerServer::PartyDanger danger = player->Server()->danger;
        return ScriptBool(function == ScriptFunction::IsPlayerInDanger ? danger == PlayerServer::PartyDanger::InDanger
                                                                       : danger == PlayerServer::PartyDanger::Safe);
    }
    case ScriptFunction::GetMercsNumber:
        return static_cast<float>(g_serverWorld.CountLivingMercs(lib::Truncate(f[0])));
    case ScriptFunction::GSGetVar: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        if (IsMultiplayerClient() && IsLive(net::g_gameServer)) {
            std::string key = stringArgs[0];
            return static_cast<float>(net::g_gameServer->GetVariable(&key));
        }
        Player* player = g_serverWorld.GetPlayer(lib::Truncate(f[0]));
        if (player) result = player->GetParty()->variables.Get(stringArgs[0]);
        return result;
    }
    case ScriptFunction::IsCameraPlaying:
        return static_cast<float>(static_cast<int>(g_clientWorld.camera->flags & Camera::kPlaying));
    case ScriptFunction::IsNight:   // IsNight (the world's day-time value, copied as a float)
        return g_serverWorld.nightLevel;
    case ScriptFunction::GetWorldTime:
        return WorldTimeOfDay();
    case ScriptFunction::IsInSquare: { // IsInSquare(x, y, x1, y1, x2, y2)
        bool inside = f[0] >= f[2] && f[0] <= f[4] && f[1] >= f[3] && f[1] <= f[5];
        return ScriptBool(inside);
    }
    case ScriptFunction::UnitInSquare: { // UnitInSquare(object, x1, y1, x2, y2)
        MapObject* o = objectArgs[0]->object;
        if (!o) return result;
        float x = o->localPosition.x, y = o->localPosition.y;
        bool inside = x >= f[0] && x <= f[2] && y >= f[1] && y <= f[3];
        return ScriptBool(inside);
    }
    case ScriptFunction::IsUnitVisible: { // IsUnitVisible: in the local player's list of units always sent
        MapObject* o = objectArgs[0]->object;
        if (!o) return result;
        Player* player = g_serverWorld.GetPlayer(0);
        if (!player) return result;
        return ScriptBool(player->Server()->partyUnits.Contains(o));
    }
    case ScriptFunction::GetZValue:
        return g_serverWorld.GridHeight(f[0], f[1]);
    case ScriptFunction::GetBSZValue:
        return g_serverWorld.RawSurfaceHeightAt(f[0], f[1]);
    case ScriptFunction::IsInArea: { // IsInArea(area, x, y)
        int area = lib::RoundEven(f[0]);
        float x = f[1], y = f[2];
        const ScriptArea* found = script->FindArea(area);
        bool inside = false;
        for (const ScriptAreaPart& part : found ? std::span(found->parts) : std::span<const ScriptAreaPart>()) {
            if (part.isRect) {
                if (x < part.x) continue;
                if (x > part.x2OrRadiusSquared) continue;
                if (y < part.y) continue;
                if (y <= part.y2) {
                    inside = true;
                    break;
                }
            } else if (SquaredDistance(x, y, part.x, part.y) < part.x2OrRadiusSquared) {
                inside = true;
                break;
            }
        }
        return ScriptBool(inside);
    }
    case ScriptFunction::IsUnitBlocked: {
        MapObject* o = objectArgs[0]->object;
        return ScriptBool(o && lib::Has(o->flags, MapObjectFlag::Blocked));
    }
    default:
        game::FatalError("Function does not return float at ScriptCall::CalcFunction");
        return result;
    }
}

void ScriptCall::CalcObject(Script* script, ScriptVars* vars, ScriptObjectRef** result) {
    PrepareArguments(script, vars);
    *result = script->NewTempObject();
    {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    const ScriptFunction function = static_cast<ScriptFunction>(id);
    switch (function) {
    case ScriptFunction::ScriptGetObject: { // GetObject(id)
        MapObject* o = g_serverWorld.FindObject(lib::Truncate(floatArgs[0]));
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", lib::Truncate(floatArgs[0]), "GetObject");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetUnitOfPlayer: { // GetUnitOfPlayer(player, index)
        Player* player = g_serverWorld.GetPlayer(lib::Truncate(floatArgs[0]));
        MapObject* o = nullptr;
        if (player) {
            const std::vector<PartyUnitRecord>& members = player->GetParty()->members;
            int index = lib::RoundEven(floatArgs[1]);
            if (index >= 0 && index < std::ssize(members))
                o = g_serverWorld.FindUnit(static_cast<int>(members[index].unitId));
        }
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetObjectByName: {
        MapObject* o = g_serverWorld.FindObject(TextKeyFromName(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with name %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByName");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetObjectByID: { // GetObjectByID (decimal string)
        MapObject* o = g_serverWorld.FindObject(str::ToInt(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with ID %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByID");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetLeader: { // GetLeader: the server's leader in multiplayer, else player 0's first unit
        int unitId;
        if (IsLive(net::g_gameServer)) {
            unitId = net::g_gameServer->LeaderId();
        } else {
            unitId = 0x3B9AF5CE;
            Player* player = g_serverWorld.GetPlayer(0);
            if (player && std::ssize(player->GetParty()->members) > 0)
                unitId = static_cast<int>(player->GetParty()->members[0].unitId);
        }
        (*result)->object = g_serverWorld.FindObject(unitId);
        return;
    }
    default:
        game::FatalError("Function does not return object at ScriptCall::CalcFunction");
        return;
    }
}

void ScriptCall::CalcGroup(Script* script, ScriptVars* vars, ScriptGroup** result) {
    PrepareArguments(script, vars);
    *result = &script->tempGroups.emplace_back();
    ScriptGroup* group = *result;
    scriptaddon::Call call{this, script, result, nullptr, nullptr};
    const bool done = scriptaddon::Dispatch(&call);
    switch (done ? ScriptFunction::None : static_cast<ScriptFunction>(id)) {
    case ScriptFunction::None:
        break;
    case ScriptFunction::GroupAdd:
        group->Union(*groupArgs[0], *groupArgs[1]);
        break;
    case ScriptFunction::GroupSub:
        group->Difference(*groupArgs[0], *groupArgs[1]);
        break;
    case ScriptFunction::GroupCross:
        group->Intersection(*groupArgs[0], *groupArgs[1]);
        break;
    case ScriptFunction::UnitSee: { // UnitSee(unit)
        group->Clear();
        Unit* unit = objectArgs[0]->AsUnit();
        if (!unit) break;
        if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        break;
    }
    case ScriptFunction::GroupSee: { // GroupSee(group): what its units see
        group->Clear();
        for (const ScriptObjectRef& member : *groupArgs[0]) {
            Unit* unit = member.AsUnit();
            if (!unit) continue;
            if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        }
        break;
    }
    case ScriptFunction::GetPlayerUnits: { // GetPlayerUnits(player): the world's units whose player compares equal
        ObjectListIterator objects;
        for (EngineObject* o = objects.First(g_serverWorld.objectManager.get()); o; o = objects.Next()) {
            Unit* unit = UnitOrNull(o);
            if (unit && static_cast<float>(unit->side) == floatArgs[0]) (*result)->Insert(unit);
        }
        break;
    }
    case ScriptFunction::PlayerSee: { // PlayerSee(player): the player's seen objects, its units and what they see
        Player* player = g_serverWorld.GetPlayer(lib::Truncate(floatArgs[0]));
        if (!player) {
            (*result)->Clear();
            break;
        }
        **result = player->Server()->possessedGroup;
        const auto& units = player->units;
        for (auto& node : units) {
            Unit* unit = node.second;
            if (!unit) break;
            (*result)->Insert(unit);
            InsertSeen(*result, unit->ServerController());
        }
        break;
    }
    default:
        game::FatalError("Function does not return string at ScriptCall::CalcFunction");
        break;
    }
}
