// Evaluation of script functions: ScriptCall::CalcFloat, CalcObject and CalcGroup (the Lua bindings' functions). Rebirth's
// functions (script_addon.h) are tried first.
// Floating point: the comparisons are plain (a NaN argument compares as IEEE says, where the
// old x87 compare counted it as equal and less).
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/app/singletons.h"
#include "game/objects/lever.h"
#include "game/objects/map_object.h"
#include "game/net/net_session.h"
#include "game/world/hoover.h"
#include "game/world/diplomacy.h"
#include "engine/data/text_manager.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/random.h"
#include "game/script/script.h"
#include "game/script/script_addon.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "engine/scene/camera.h"

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

// (float)(time + add) rounded to nearest: the sum (the int loaded whole) is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    return lib::RoundEven(static_cast<float>(time) + extra);
}

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

Player* ScriptArgs::FindPlayer() { return g_serverWorld.GetPlayer(lib::Truncate(Float())); }

float ScriptCall::CalcFloat(Script* script) {
    float result = 0.0f;
    {
        scriptaddon::Call call{this, script, &result};
        if (scriptaddon::Dispatch(&call)) return result;
    }
    ScriptArgs args(this);
    const ScriptFunction function = id;
    switch (function) {
    case ScriptFunction::IsEqual: {
        const float a = args.Float();
        const float b = args.Float();
        return ScriptBool(a == b);
    }
    case ScriptFunction::IsLess: {
        const float a = args.Float();
        const float b = args.Float();
        return ScriptBool(a < b);
    }
    case ScriptFunction::IsGreater: {
        const float a = args.Float();
        const float b = args.Float();
        return ScriptBool(a > b);
    }
    case ScriptFunction::IsEqualString: {
        const std::string& a = args.Text();
        const std::string& b = args.Text();
        return ScriptBool(str::Compare(a, b) == 0);
    }
    case ScriptFunction::Add: {
        const float a = args.Float();
        const float b = args.Float();
        return b + a;
    }
    case ScriptFunction::Sub: {
        const float a = args.Float();
        const float b = args.Float();
        return a - b;
    }
    case ScriptFunction::Mul: {
        const float a = args.Float();
        const float b = args.Float();
        return b * a;
    }
    case ScriptFunction::Div: {
        const float a = args.Float();
        const float b = args.Float();
        return a / b;
    }
    case ScriptFunction::Not:
        return ScriptBool(args.Float() == 0.0f);

    case ScriptFunction::HP:
    case ScriptFunction::MaxHP:
    case ScriptFunction::Mana:
    case ScriptFunction::MaxMana: {
        MapObject* o = args.Object().object;
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
        MapObject* o = args.Object().object;
        if (o) result = function == ScriptFunction::GetX ? o->localPosition.x : o->localPosition.y;
        else result = 0.0f;
        return result;
    }
    case ScriptFunction::GetZ: { // GetZ: height above the ground at the object's position
        MapObject* o = args.Object().object;
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
        Player* player = args.FindPlayer();
        if (player) result = static_cast<float>(function == ScriptFunction::GetLootItemsCount ? std::ssize(player->items) : player->GetParty()->money);
        return result;
    }
    case ScriptFunction::WasLooted: {
        MapObject* o = args.Object().object;
        return ScriptBool(!o || lib::Has(o->flags, MapObjectFlag::LootTaken));
    }
    case ScriptFunction::GetFutureX:
    case ScriptFunction::GetFutureY: { // GetFutureX / GetFutureY(object, ticks ahead)
        MapObject* o = args.Object().object;
        if (!o) {
            return 0.0f;
        }
        Vector3 position = o->PositionAt(RoundTimeOffset(g_serverWorld.tick, args.Float()));
        return function == ScriptFunction::GetFutureX ? position.x : position.y;
    }
    case ScriptFunction::IsUnitInWater: { // IsUnitInWater: flag 0x80 of the AI map cell at the object's position
        MapObject* o = args.Object().object;
        return ScriptBool(o && g_serverWorld.aiMap.InWaterAt(o->localPosition));
    }
    case ScriptFunction::GroupSize:
        return static_cast<float>(args.Group()->size());
    case ScriptFunction::GroupHas: { // GroupHas(group, object)
        ScriptGroup* group = args.Group();
        MapObject* o = args.Object().object;
        return ScriptBool(group->Contains(o));
    }
    case ScriptFunction::Random: { // Random(range): [0, range]
        float range = args.Float();
        std::uint32_t value = g_random.Next();
        return ScaleWord(value, range);
    }
    case ScriptFunction::GetDiplomacy: { // GetDiplomacy(a, b): 1 allied, -1 enemies, 0 otherwise
        int a = lib::Truncate(args.Float());
        int b = lib::Truncate(args.Float());
        DiplomacyRelation relation = g_serverWorld.diplomacy.GetRelation(a, b);
        if (relation == DiplomacyRelation::Hostile) result = -1.0f;
        else if (relation == DiplomacyRelation::Allied) result = 1.0f;
        return result;
    }
    case ScriptFunction::IsAlive:
    case ScriptFunction::IsDead: {
        MapObject* o = args.Object().object;
        MapObjectCompanion* stats = o ? o->companion.Get() : nullptr;
        if (!stats) return result;
        int state = stats->IsDead();
        return function == ScriptFunction::IsAlive ? ScriptBool(state == 0) : static_cast<float>(state);
    }
    case ScriptFunction::IsEnemy: { // IsEnemy(unit, player)
        Unit* unit = args.Object().AsUnit();
        if (!unit) return result;
        int player = lib::Truncate(args.Float());
        std::uint32_t mask = 1u << (player & 31);
        bool enemy = (mask & unit->relationsB & unit->relationsA) != 0;
        return ScriptBool(enemy);
    }
    case ScriptFunction::GetPlayer: {
        Unit* unit = args.Object().AsUnit();
        if (unit) result = static_cast<float>(unit->side);
        return result;
    }
    case ScriptFunction::GetAIClass: { // GetAIClass (the unit's size class)
        Unit* unit = args.Object().AsUnit();
        result = -1.0f;
        if (unit) result = static_cast<float>(unit->sizeClass);
        return result;
    }
    case ScriptFunction::GetLeverState: {
        if (Lever* lever = LeverOrNull(args.Object().object)) result = static_cast<float>(lever->state);
        return result;
    }
    case ScriptFunction::GetObjectID: {
        MapObject* o = args.Object().object;
        if (o) result = static_cast<float>(o->id);
        return result;
    }
    case ScriptFunction::IsAlarm:
        return ScriptBool(Alarm(lib::RoundEven(args.Float())).active != 0);
    case ScriptFunction::AlarmTime:
        return static_cast<float>(Alarm(lib::RoundEven(args.Float())).time);
    case ScriptFunction::AlarmPosX:
        return Alarm(lib::RoundEven(args.Float())).x;
    case ScriptFunction::AlarmPosY:
        return Alarm(lib::RoundEven(args.Float())).y;
    case ScriptFunction::DistanceUnitUnit: {
        MapObject* a = args.Object().object;
        MapObject* b = args.Object().object;
        if (a && b) result = Distance(XY(a->localPosition), XY(b->localPosition));
        else result = kFar;
        return result;
    }
    case ScriptFunction::DistanceUnitPoint: { // DistanceUnitPoint(object, x, y)
        MapObject* o = args.Object().object;
        const float x = args.Float();
        const float y = args.Float();
        if (o) result = Distance(XY(o->localPosition), Vector2{x, y});
        else result = kFar;
        return result;
    }

    case ScriptFunction::HaveItem: { // HaveItem(player, item)
        Player* player = args.FindPlayer();
        const int item = lib::Truncate(args.Float());
        return ScriptBool(player && player->HasLoot(item));
    }
    case ScriptFunction::IsPlayerInDanger:
    case ScriptFunction::IsPlayerInSafety: {
        Player* player = args.FindPlayer();
        if (!player) return result;
        PlayerServer::PartyDanger danger = player->Server()->danger;
        return ScriptBool(function == ScriptFunction::IsPlayerInDanger ? danger == PlayerServer::PartyDanger::InDanger
                                                                       : danger == PlayerServer::PartyDanger::Safe);
    }
    case ScriptFunction::GetMercsNumber:
        return static_cast<float>(g_serverWorld.CountLivingMercs(lib::Truncate(args.Float())));
    case ScriptFunction::GSGetVar: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        if (IsMultiplayerClient() && IsLive(net::g_gameServer)) {
            args.Float();   // the player is not used
            std::string key = args.Text();
            return static_cast<float>(net::g_gameServer->GetVariable(key));
        }
        Player* player = args.FindPlayer();
        if (player) result = player->GetParty()->variables.Get(args.Text());
        return result;
    }
    case ScriptFunction::IsCameraPlaying:
        return static_cast<float>(static_cast<int>(g_clientWorld.camera->flags & Camera::kPlaying));
    case ScriptFunction::IsNight:   // IsNight (the world's day-time value, copied as a float)
        return g_serverWorld.nightLevel;
    case ScriptFunction::GetWorldTime:
        return WorldTimeOfDay();
    case ScriptFunction::IsInSquare: { // IsInSquare(x, y, x1, y1, x2, y2)
        const float x = args.Float();
        const float y = args.Float();
        const float x1 = args.Float();
        const float y1 = args.Float();
        const float x2 = args.Float();
        const float y2 = args.Float();
        bool inside = x >= x1 && x <= x2 && y >= y1 && y <= y2;
        return ScriptBool(inside);
    }
    case ScriptFunction::UnitInSquare: { // UnitInSquare(object, x1, y1, x2, y2)
        MapObject* o = args.Object().object;
        if (!o) return result;
        const float x1 = args.Float();
        const float y1 = args.Float();
        const float x2 = args.Float();
        const float y2 = args.Float();
        float x = o->localPosition.x, y = o->localPosition.y;
        bool inside = x >= x1 && x <= x2 && y >= y1 && y <= y2;
        return ScriptBool(inside);
    }
    case ScriptFunction::IsUnitVisible: { // IsUnitVisible: in the local player's list of units always sent
        MapObject* o = args.Object().object;
        if (!o) return result;
        Player* player = g_serverWorld.GetPlayer(0);
        if (!player) return result;
        return ScriptBool(player->Server()->partyUnits.Contains(o));
    }
    case ScriptFunction::GetZValue:
    case ScriptFunction::GetBSZValue: { // GetZValue / GetBSZValue(x, y)
        const float x = args.Float();
        const float y = args.Float();
        return function == ScriptFunction::GetZValue ? g_serverWorld.GridHeight(x, y) : g_serverWorld.RawSurfaceHeightAt(x, y);
    }
    case ScriptFunction::IsInArea: { // IsInArea(area, x, y)
        int area = lib::RoundEven(args.Float());
        const float x = args.Float();
        const float y = args.Float();
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
        MapObject* o = args.Object().object;
        return ScriptBool(o && lib::Has(o->flags, MapObjectFlag::Blocked));
    }
    default:
        game::FatalExit("Function does not return float at ScriptCall::CalcFunction");
    }
}

void ScriptCall::CalcObject(Script* script, ScriptObjectRef** result) {
    *result = script->NewTempObject();
    {
        scriptaddon::Call call{this, script, result};
        if (scriptaddon::Dispatch(&call)) return;
    }
    ScriptArgs args(this);
    const ScriptFunction function = id;
    switch (function) {
    case ScriptFunction::ScriptGetObject: { // GetObject(id)
        const int objectId = lib::Truncate(args.Float());
        MapObject* o = g_serverWorld.FindObject(objectId);
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", objectId, "GetObject");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetUnitOfPlayer: { // GetUnitOfPlayer(player, index)
        Player* player = args.FindPlayer();
        MapObject* o = nullptr;
        if (player) {
            const std::vector<PartyUnitRecord>& members = player->GetParty()->members;
            int index = lib::RoundEven(args.Float());
            if (index >= 0 && index < std::ssize(members))
                o = g_serverWorld.FindUnit(static_cast<int>(members[index].unitId));
        }
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetObjectByName: {
        const std::string& name = args.Text();
        MapObject* o = g_serverWorld.FindObject(TextKeyFromName(name));
        if (!o) game::DebugLog("Object with name %s not found in function \"%s\".\n", name, "GetObjectByName");
        (*result)->object = o;
        return;
    }
    case ScriptFunction::GetObjectByID: { // GetObjectByID (decimal string)
        const std::string& text = args.Text();
        MapObject* o = g_serverWorld.FindObject(str::ToInt(text));
        if (!o) game::DebugLog("Object with ID %s not found in function \"%s\".\n", text, "GetObjectByID");
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
        game::FatalExit("Function does not return object at ScriptCall::CalcFunction");
    }
}

void ScriptCall::CalcGroup(Script* script, ScriptGroup** result) {
    *result = &script->tempGroups.emplace_back();
    ScriptGroup* group = *result;
    scriptaddon::Call call{this, script, result};
    const bool done = scriptaddon::Dispatch(&call);
    ScriptArgs args(this);
    switch (done ? ScriptFunction::None : id) {
    case ScriptFunction::None:
        break;
    case ScriptFunction::GroupAdd: {
        ScriptGroup* a = args.Group();
        ScriptGroup* b = args.Group();
        group->Union(*a, *b);
        break;
    }
    case ScriptFunction::GroupSub: {
        ScriptGroup* a = args.Group();
        ScriptGroup* b = args.Group();
        group->Difference(*a, *b);
        break;
    }
    case ScriptFunction::GroupCross: {
        ScriptGroup* a = args.Group();
        ScriptGroup* b = args.Group();
        group->Intersection(*a, *b);
        break;
    }
    case ScriptFunction::UnitSee: { // UnitSee(unit)
        group->Clear();
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        break;
    }
    case ScriptFunction::GroupSee: { // GroupSee(group): what its units see
        group->Clear();
        for (const ScriptObjectRef& member : *args.Group()) {
            Unit* unit = member.AsUnit();
            if (!unit) continue;
            if (UnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        }
        break;
    }
    case ScriptFunction::GetPlayerUnits: { // GetPlayerUnits(player): the world's units whose player compares equal
        const float player = args.Float();
        ObjectListIterator objects;
        for (EngineObject* o = objects.First(g_serverWorld.objectManager.get()); o; o = objects.Next()) {
            Unit* unit = UnitOrNull(o);
            if (unit && static_cast<float>(unit->side) == player) (*result)->Insert(unit);
        }
        break;
    }
    case ScriptFunction::PlayerSee: { // PlayerSee(player): the player's seen objects, its units and what they see
        Player* player = args.FindPlayer();
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
        game::FatalExit("Function does not return string at ScriptCall::CalcFunction");
    }
}
