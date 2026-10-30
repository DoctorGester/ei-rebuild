// Evaluation of script functions: SSCCmd::CalcParameters, CalcFloat, CalcObject and CalcGroup. Rebirth's
// AddNewScriptCommands dispatches to addon's functions first.
// Floating point: the comparisons are plain (a NaN argument compares as IEEE says, where the
// old x87 compare counted it as equal and less: the NaN audit, lib/nan_audit.h). The game's x87
// code ran at 24-bit precision, where every operation rounds to a float: float maths here, and
// lib/pc24.h where an operand is an int or a double that a float cannot hold.
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
#include "world_glue.h"

#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include "ei/rebirth.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// ---- Conversions --------------------------------------------------------------------------------

float Distance(float x0, float y0, float x1, float y1) { return Length2D(Vector2{x0, y0} - Vector2{x1, y1}); }

// As IsInArea tests circles.
float SquaredDistance(float x, float y, float centreX, float centreY) {
    return DistanceSquared2D(Vector2{x, y}, Vector2{centreX, centreY});
}

// value * range / (2^32 - 1): the game multiplies the 32-bit value (loaded whole) by the range, then by the double
// 1 / (2^32 - 1) = 2^-32 (1 + 2^-32), which rounds the rounded product to the product times 2^-32: the same as the
// range times value * 2^-32 (exact in a double) rounded once.
float ScaleRandom(std::uint32_t value, float range) { return pc24::Mul(range, value * 0x1p-32); }

// (float)(time + add) rounded to nearest: the sum (the int loaded whole) is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    return lib::RoundEven(pc24::Add(static_cast<double>(time), extra));
}

// The game's _ftol: truncating conversion; out-of-range values keep the low 32 bits
// of the 64-bit result (0 for NaN).
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }

constexpr float kMinusOne = -1.0f;
constexpr float kFar = 1e30f;     // the distance when an object is missing
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CA);

// (float)(int)b: converts 0/1 exactly.
float FromBool(bool value) { return static_cast<float>(value ? 1 : 0); }

// ---- Game state reached by the functions --------------------------------------------------------



// The game server session when one runs and is not shut down.
net::ServerSession* ActiveServer() {
    net::ServerSession* server = net::g_gameServer;
    if (server && !(server->flags7 & RefCounted::kDetached)) return server;
    return nullptr;
}

WorldAlarm& Alarm(int index) { return g_serverWorld.alarms[index]; }

// GetWorldTime: (time * scale + offset) mod 24 (the tick loaded whole; fmod is exact).
float WorldTimeOfDay() {
    const WorldServer* world = &g_serverWorld;
    return std::fmod(pc24::Mul(world->secondsPerTick, world->tick) + world->timeBase, 24.0f);
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

void SSCCmd::PrepareArguments(Script* script, ScriptVars* vars) {
    SetParameters(localBindings, vars);
    SetParameters(globalBindings, &script->globals);
    for (int i = 0; i < std::ssize(subCalls); ++i) CalcParameters(script, vars, &subCalls[i]);
}

void SSCCmd::CalcParameters(Script* script, ScriptVars* vars, const ScriptSubCall* call) {
    switch (call->type) {
    case kScriptFloat:
        call->cmd->CalcFloat(script, vars, &floatArgs[call->argIndex]);
        return;
    case kScriptString: {
        // Rebirth's AddNewScriptCommands calls SSCCmd::CalcString, which
        // runs addon's string functions.
        if (scriptaddon::Enabled()) {
            scriptaddon::CalcString(call->cmd.get(), script, vars, &stringArgs[call->argIndex]);
            return;
        }
        // No function returns a string: the arguments are still evaluated before the error.
        call->cmd->PrepareArguments(script, vars);
        game::FatalError("Function does not return string at SSCCmd::CalcFunction");
        return;
    }
    case kScriptObject:
        call->cmd->CalcObject(script, vars, &objectArgs[call->argIndex]);
        return;
    case kScriptGroup:
        call->cmd->CalcGroup(script, vars, &groupArgs[call->argIndex]);
        return;
    default:
        game::FatalError("SSCCmd::CalcParameters, unknown function parameter type");
    }
}

void SSCCmd::CalcFloat(Script* script, ScriptVars* vars, float* result) {
    PrepareArguments(script, vars);
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    const float* f = floatArgs.data();
    const ScriptFunction function = static_cast<ScriptFunction>(id);
    switch (function) {
    case ScriptFunction::IsEqual:
        EI_NAN_AUDIT(f[0], f[1]);
        *result = FromBool(f[0] == f[1]);
        return;
    case ScriptFunction::IsLess:
        EI_NAN_AUDIT(f[0], f[1]);
        *result = FromBool(f[0] < f[1]);
        return;
    case ScriptFunction::IsGreater:
        *result = FromBool(f[0] > f[1]);
        return;
    case ScriptFunction::IsEqualString:
        *result = FromBool(str::Compare(stringArgs[0], stringArgs[1]) == 0);
        return;
    case ScriptFunction::Add:
        *result = f[1] + f[0];
        return;
    case ScriptFunction::Sub:
        *result = f[0] - f[1];
        return;
    case ScriptFunction::Mul:
        *result = f[1] * f[0];
        return;
    case ScriptFunction::Div:
        *result = f[0] / f[1];
        return;
    case ScriptFunction::Not:
        EI_NAN_AUDIT(f[0]);
        *result = FromBool(f[0] == 0.0f);
        return;

    case ScriptFunction::HP:
    case ScriptFunction::MaxHP:
    case ScriptFunction::Mana:
    case ScriptFunction::MaxMana: {
        EngineObject* o = objectArgs[0]->object;
        MapObjectServer* stats = o ? ObjectSlots(o)->ServerCompanion() : nullptr;
        if (stats) {
            const UnitVitals& vitals = stats->vitals;
            const float* value = function == ScriptFunction::HP ? &vitals.hitPoints
                               : function == ScriptFunction::MaxHP ? &vitals.maxHitPoints
                               : function == ScriptFunction::Mana ? &vitals.mana
                                            : &vitals.maxMana;
            *result = *value;
        } else {
            *result = 0.0f;
        }
        return;
    }
    case ScriptFunction::GetX:
    case ScriptFunction::GetY: {
        EngineObject* o = objectArgs[0]->object;
        if (o) *result = function == ScriptFunction::GetX ? ObjectSlots(o)->localPosition.x : ObjectSlots(o)->localPosition.y;
        else *result = 0.0f;
        return;
    }
    case ScriptFunction::GetZ: { // GetZ: height above the ground at the object's position
        EngineObject* o = objectArgs[0]->object;
        if (!o) {
            *result = 0.0f;
            return;
        }
        float x = ObjectSlots(o)->localPosition.x, y = ObjectSlots(o)->localPosition.y, z = ObjectSlots(o)->localPosition.z;
        if (CUnit* unit = UnitOrNull(o)) *result = pc24::Add(unit->GroundHeight(x, y), z);   // (o is not null)
        else *result = g_serverWorld.GridHeight(x, y) + z;
        return;
    }
    case ScriptFunction::GetLootItemsCount:
    case ScriptFunction::GetMoney: {
        *result = 0.0f;
        CPlayer* player = g_serverWorld.GetPlayer(Truncate(f[0]));
        if (player) *result = static_cast<float>(function == ScriptFunction::GetLootItemsCount ? std::ssize(player->items) : player->GetParty()->money);
        return;
    }
    case ScriptFunction::WasLooted: {
        *result = 1.0f;
        EngineObject* o = objectArgs[0]->object;
        if (o) *result = static_cast<float>(lib::Has(o->flags, MapObjectFlag::LootTaken));
        return;
    }
    case ScriptFunction::GetFutureX:
    case ScriptFunction::GetFutureY: {
        EngineObject* o = objectArgs[0]->object;
        if (!o) {
            *result = 0.0f;
            return;
        }
        Vector3 buffer;
        const Vector3* position = ObjectSlots(o)->PositionAt(&buffer, RoundTimeOffset(g_serverWorld.tick, f[0]));
        *result = *(function == ScriptFunction::GetFutureX ? &position->x : &position->y);
        return;
    }
    case ScriptFunction::IsUnitInWater: { // IsUnitInWater: flag 0x80 of the AI map cell at the object's position
        EngineObject* o = objectArgs[0]->object;
        int inWater = 0;
        if (o) inWater = g_serverWorld.aiMap.InWaterAt(ObjectSlots(o)->localPosition) ? 1 : 0;
        *result = static_cast<float>(inWater);
        return;
    }
    case ScriptFunction::GroupSize:
        *result = static_cast<float>(groupArgs[0]->size());
        return;
    case ScriptFunction::GroupHas: {
        EngineObject* o = objectArgs[0]->object;
        *result = groupArgs[0]->Contains(o) ? 1.0f : 0.0f;
        return;
    }
    case ScriptFunction::Random: { // Random(range): [0, range]
        float range = f[0];
        std::uint32_t value = g_random.Next();
        *result = ScaleRandom(value, range);
        return;
    }
    case ScriptFunction::GetDiplomacy: { // GetDiplomacy: 1 allied, -1 enemies, 0 otherwise
        int b = Truncate(f[1]);
        int a = Truncate(f[0]);
        int relation = g_serverWorld.diplomacy.GetRelation(a, b);
        *result = 0.0f;
        if (relation == 2) *result = kMinusOne;
        else if (relation == 0) *result = 1.0f;
        return;
    }
    case ScriptFunction::IsAlive:
    case ScriptFunction::IsDead: {
        EngineObject* o = objectArgs[0]->object;
        MapObjectCompanion* stats = o ? ObjectSlots(o)->companion : nullptr;
        *result = 0.0f;
        if (!stats) return;
        int state = stats->IsDead();
        *result = static_cast<float>(function == ScriptFunction::IsAlive ? (state == 0 ? 1 : 0) : state);
        return;
    }
    case ScriptFunction::IsEnemy: { // IsEnemy(unit, player)
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = 0.0f;
        if (!unit) return;
        int player = Truncate(f[0]);
        std::uint32_t mask = 1u << (player & 31);
        bool enemy = (mask & unit->relationsB & unit->relationsA) != 0;
        *result = static_cast<float>(enemy ? 1 : 0);
        return;
    }
    case ScriptFunction::GetPlayer: {
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = 0.0f;
        if (unit) *result = static_cast<float>(unit->side);
        return;
    }
    case ScriptFunction::GetAIClass: { // GetAIClass (the unit's size class)
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = kMinusOne;
        if (unit) *result = static_cast<float>(unit->sizeClass);
        return;
    }
    case ScriptFunction::GetLeverState: {
        EngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o || o->GetClassId() != ClassId::Lever) return;
        *result = static_cast<float>(static_cast<Lever*>(o)->state);
        return;
    }
    case ScriptFunction::GetObjectID: {
        EngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (o) *result = static_cast<float>(o->id);
        return;
    }
    case ScriptFunction::IsAlarm:
        *result = FromBool(Alarm(lib::RoundEven(f[0])).active != 0);
        return;
    case ScriptFunction::AlarmTime:
        *result = static_cast<float>(Alarm(lib::RoundEven(f[0])).time);
        return;
    case ScriptFunction::AlarmPosX:
        *result = Alarm(lib::RoundEven(f[0])).x;
        return;
    case ScriptFunction::AlarmPosY:
        *result = Alarm(lib::RoundEven(f[0])).y;
        return;
    case ScriptFunction::DistanceUnitUnit: {
        EngineObject* a = objectArgs[0]->object;
        EngineObject* b = objectArgs[1]->object;
        if (a && b) *result = Distance(ObjectSlots(a)->localPosition.x, ObjectSlots(a)->localPosition.y, ObjectSlots(b)->localPosition.x, ObjectSlots(b)->localPosition.y);
        else *result = kFar;
        return;
    }
    case ScriptFunction::DistanceUnitPoint: {
        EngineObject* o = objectArgs[0]->object;
        if (o) *result = Distance(ObjectSlots(o)->localPosition.x, ObjectSlots(o)->localPosition.y, f[0], f[1]);
        else *result = kFar;
        return;
    }

    // Every/Any/Sum(var, group, condition): the condition (the nested float call) is evaluated
    // again for each member of the group with the object variable set to that member. Without a
    // nested call the condition is a constant.
    case ScriptFunction::Every:
    case ScriptFunction::Any:
    case ScriptFunction::Sum: {
        const ScriptSubCall* condition = nullptr;
        for (int i = 0; i < std::ssize(subCalls); ++i)
            if (subCalls[i].type == kScriptFloat) {
                condition = &subCalls[i];
                break;
            }
        if (!condition) {
            if (function == ScriptFunction::Every) *result = floatArgs[0];
            else if (function == ScriptFunction::Any) {
                EI_NAN_AUDIT(floatArgs[0]);
                *result = FromBool(floatArgs[0] != 0.0f);
            }
            else *result = static_cast<float>(groupArgs[0]->size()) * floatArgs[0];
            return;
        }
        *result = function == ScriptFunction::Every ? 1.0f : 0.0f;
        ScriptGroup* g = groupArgs[0];
        if (g->empty()) return;
        int k = 0;
        do {
            *objectArgs[0] = (*g)[static_cast<std::size_t>(k)];
            CalcParameters(script, vars, condition);
            if (function == ScriptFunction::Every) {
                EI_NAN_AUDIT(floatArgs[0]);
                if (floatArgs[0] == 0.0f) {
                    *result = 0.0f;
                    return;
                }
            } else if (function == ScriptFunction::Any) {
                EI_NAN_AUDIT(floatArgs[0]);
                if (floatArgs[0] != 0.0f) {
                    *result = 1.0f;
                    return;
                }
            } else {
                *result = floatArgs[0] + *result;
            }
            ++k;
            g = groupArgs[0];
        } while (k < std::ssize(*g));
        return;
    }

    case ScriptFunction::HaveItem: { // HaveItem(player, item)
        *result = 0.0f;
        auto* player = g_serverWorld.GetPlayer(Truncate(f[0]));
        if (player && player->HasLoot(Truncate(f[1]))) *result = 1.0f;
        return;
    }
    case ScriptFunction::IsPlayerInDanger:
    case ScriptFunction::IsPlayerInSafety: {
        *result = 0.0f;
        CPlayer* player = g_serverWorld.GetPlayer(Truncate(f[0]));
        if (!player) return;
        PlayerServer::PartyDanger danger = player->Server()->danger;
        if (function == ScriptFunction::IsPlayerInDanger ? danger == PlayerServer::PartyDanger::InDanger
                                                         : danger == PlayerServer::PartyDanger::Safe)
            *result = 1.0f;
        return;
    }
    case ScriptFunction::GetMercsNumber:
        *result = static_cast<float>(g_serverWorld.CountLivingMercs(Truncate(f[0])));
        return;
    case ScriptFunction::GSGetVar: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        net::ServerSession* server = IsMultiplayerClient() ? ActiveServer() : nullptr;
        if (server) {
            std::string key = stringArgs[0];
            *result = static_cast<float>(server->GetVariable(&key));
            return;
        }
        *result = 0.0f;
        CPlayer* player = g_serverWorld.GetPlayer(Truncate(f[0]));
        if (player) *result = player->GetParty()->variables.Get(stringArgs[0]);
        return;
    }
    case ScriptFunction::IsCameraPlaying:
        *result = static_cast<float>(static_cast<int>(GameCameraPathPlaying()));
        return;
    case ScriptFunction::IsNight:   // IsNight (the world's day-time value, copied as a float)
        *result = g_serverWorld.nightLevel;
        return;
    case ScriptFunction::GetWorldTime:
        *result = WorldTimeOfDay();
        return;
    case ScriptFunction::IsInSquare: { // IsInSquare(x, y, x1, y1, x2, y2)
        EI_NAN_AUDIT(f[0], f[1], f[2], f[3], f[4], f[5]);
        bool inside = f[0] >= f[2] && f[0] <= f[4] && f[1] >= f[3] && f[1] <= f[5];
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case ScriptFunction::UnitInSquare: { // UnitInSquare(object, x1, y1, x2, y2)
        EngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o) return;
        float x = ObjectSlots(o)->localPosition.x, y = ObjectSlots(o)->localPosition.y;
        EI_NAN_AUDIT(x, y, f[0], f[1], f[2], f[3]);
        bool inside = x >= f[0] && x <= f[2] && y >= f[1] && y <= f[3];
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case ScriptFunction::IsUnitVisible: { // IsUnitVisible: in the local player's list of units always sent
        EngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o) return;
        CPlayer* player = g_serverWorld.GetPlayer(0);
        if (!player) return;
        *result = static_cast<float>(player->Server()->partyUnits.Contains(o) ? 1 : 0);
        return;
    }
    case ScriptFunction::GetZValue:
        *result = g_serverWorld.GridHeight(f[0], f[1]);
        return;
    case ScriptFunction::GetBSZValue:
        *result = static_cast<float>(g_serverWorld.RawSurfaceHeightAt(f[0], f[1]));
        return;
    case ScriptFunction::IsInArea: { // IsInArea(area, x, y)
        int area = lib::RoundEven(f[0]);
        float x = f[1], y = f[2];
        const ScriptArea* parts = script->FindArea(area);
        bool inside = false;
        for (const ScriptAreaPart& part : parts->parts) {
            EI_NAN_AUDIT(x, y, part.x, part.y, part.x2OrRadiusSquared);
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
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case ScriptFunction::IsUnitBlocked: {
        EngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (o) *result = static_cast<float>(lib::Has(o->flags, MapObjectFlag::Blocked));
        return;
    }
    default:
        game::FatalError("Function does not return float at SSCCmd::CalcFunction");
        return;
    }
}

void SSCCmd::CalcObject(Script* script, ScriptVars* vars, ScriptObjectRef** result) {
    PrepareArguments(script, vars);
    *result = script->NewTempObject();
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    const ScriptFunction function = static_cast<ScriptFunction>(id);
    switch (function) {
    case ScriptFunction::ScriptGetObject: { // GetObject(id)
        EngineObject* o = g_serverWorld.FindObject(Truncate(floatArgs[0]));
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", Truncate(floatArgs[0]), "GetObject");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetUnitOfPlayer: { // GetUnitOfPlayer(player, index)
        CPlayer* player = g_serverWorld.GetPlayer(Truncate(floatArgs[0]));
        EngineObject* o = nullptr;
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
        EngineObject* o = g_serverWorld.FindObject(TextKeyFromName(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with name %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByName");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetObjectByID: { // GetObjectByID (decimal string)
        EngineObject* o = g_serverWorld.FindObject(str::ToInt(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with ID %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByID");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetLeader: { // GetLeader: the server's leader in multiplayer, else player 0's first unit
        int unitId;
        if (net::ServerSession* server = ActiveServer()) {
            unitId = server->LeaderId();
        } else {
            unitId = 0x3B9AF5CE;
            CPlayer* player = g_serverWorld.GetPlayer(0);
            if (player && std::ssize(player->GetParty()->members) > 0)
                unitId = static_cast<int>(player->GetParty()->members[0].unitId);
        }
        (*result)->object = g_serverWorld.FindObject(unitId);
        return;
    }
    default:
        game::FatalError("Function does not return object at SSCCmd::CalcFunction");
        return;
    }
}

void SSCCmd::CalcGroup(Script* script, ScriptVars* vars, ScriptGroup** result) {
    ObjectListIterator objects;
    objects.manager = nullptr;
    PrepareArguments(script, vars);
    *result = &script->tempGroups.emplace_back();
    ScriptGroup* group = *result;
    bool done = false;
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        done = scriptaddon::Dispatch(&call);
    }
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
        CUnit* unit = objectArgs[0]->AsUnit();
        if (!unit) break;
        if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        break;
    }
    case ScriptFunction::GroupSee: { // GroupSee(group): what its units see
        group->Clear();
        for (const ScriptObjectRef& member : *groupArgs[0]) {
            CUnit* unit = member.AsUnit();
            if (!unit) continue;
            if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        }
        break;
    }
    case ScriptFunction::GetPlayerUnits: { // GetPlayerUnits(player): the world's units whose player compares equal
        EI_NAN_AUDIT(floatArgs[0]);
        for (EngineObject* o = objects.First(g_serverWorld.objectManager); o; o = objects.Next()) {
            CUnit* unit = UnitOrNull(o);
            if (unit && static_cast<double>(unit->side) == floatArgs[0]) (*result)->Insert(o);
        }
        break;
    }
    case ScriptFunction::PlayerSee: { // PlayerSee(player): the player's seen objects, its units and what they see
        CPlayer* player = g_serverWorld.GetPlayer(Truncate(floatArgs[0]));
        if (!player) {
            (*result)->Clear();
            break;
        }
        **result = player->Server()->possessedGroup;
        const auto& units = player->units;
        for (auto& node : units) {
            CUnit* unit = node.second;
            if (!unit) break;
            (*result)->Insert(unit);
            InsertSeen(*result, unit->ServerController());
        }
        break;
    }
    default:
        game::FatalError("Function does not return string at SSCCmd::CalcFunction");
        break;
    }
    if (objects.manager) --objects.manager->refCount;    // the iterator's destructor
}
