// Evaluation of script functions: SSCCmd::CalcParameters, CalcFloat, CalcObject and CalcGroup. Rebirth's
// AddNewScriptCommands dispatches to addon's functions first.
// Floating point: the comparisons are plain (a NaN argument compares as IEEE says, where the
// old x87 compare counted it as equal and less: the NaN audit, lib/nan_audit.h); the multi-step
// computations keep their intermediates unrounded (double locals), as x87 registers would.
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

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// ---- x87 helpers --------------------------------------------------------------------------------

// sqrt(dy * dy + dx * dx) with dx = x0 - x1 and dy = y0 - y1 kept unrounded (in registers).
float Distance(float x0, float y0, float x1, float y1) {
    const double dx = static_cast<double>(x0) - x1;
    const double dy = static_cast<double>(y0) - y1;
    return static_cast<float>(std::sqrt(LengthSquared2DUnrounded(dy, dx)));
}

// (dy * dy + dx * dx) unrounded, as IsInArea tests circles.
double SquaredDistance(float x, float y, float centreX, float centreY) {
    const double dx = static_cast<double>(x) - centreX;
    const double dy = static_cast<double>(y) - centreY;
    return LengthSquared2DUnrounded(dy, dx);
}

// 1 / (2^32 - 1).
const std::uint64_t kRandomScaleBits = 0x3DF0000000100000ull;

// value * range / (2^32 - 1): the 32-bit value loaded as a 64-bit integer, two multiplications.
float ScaleRandom(std::uint32_t value, float range) {
    return static_cast<float>(x87::FromUnsigned(value) * range * std::bit_cast<double>(kRandomScaleBits));
}

// (float)(time + add) rounded to nearest: the sum is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    return x87::fistp(static_cast<float>(static_cast<double>(time) + extra));
}

// The game's _ftol: truncating conversion; out-of-range values keep the low 32 bits
// of the 64-bit result.
int Ftol(float value) { return static_cast<int>(libcrt::Ftol(value)); }

constexpr float kMinusOne = -1.0f;
constexpr float kFar = 1e30f;     // the distance when an object is missing
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CA);

// (float)(int)b: converts 0/1 exactly.
float FromBool(bool b) { return static_cast<float>(b ? 1 : 0); }

// ---- Game state reached by the functions --------------------------------------------------------

// The companion holding a map object's vitals. Scripts run on the server, so it is the
// CMapObjectServer (not checked).
CMapObjectServer* ServerHalf(CEngineObject* object) {
    return ObjectSlots(object)->ServerCompanion();
}

// The ground (or water surface) height under (x, y) for the unit; the result stays unrounded.
double UnitGroundHeight(CUnit* unit, float x, float y) { return unit->GroundHeight(x, y); }

// The game server session when one runs and is not shut down.
net::ServerSession* ActiveServer() {
    net::ServerSession* server = net::g_gameServer;
    if (server && !(server->flags7_ & CRefCounted::kDetached)) return server;
    return nullptr;
}

WorldAlarm& Alarm(int index) { return g_serverWorld->alarms_[index]; }

// GetWorldTime: (time * scale + offset) mod 24.
float WorldTimeOfDay() {
    const CWorldServer* world = &g_serverWorld.Get();
    return static_cast<float>(std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, 24.0));
}

// What a unit sees: the AI brain's visible list.
void InsertSeen(ScriptGroup* group, CUnitServer* controller) {
    const auto& visible = controller->brain.memory.visible;
    for (auto node = visible.begin(); node != visible.end();) {
        auto current = node;
        node = std::next(current);
        group->Insert(*current);
    }
}

} // namespace

void SSCCmd::PrepareArguments(CScript* script, ScriptVars* vars) {
    SetParameters(localBindings, vars);
    SetParameters(globalBindings, &script->globals_);
    for (int i = 0; i < std::ssize(subCalls); ++i) CalcParameters(script, vars, &subCalls[i]);
}

void SSCCmd::CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call) {
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

void SSCCmd::CalcFloat(CScript* script, ScriptVars* vars, float* result) {
    PrepareArguments(script, vars);
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    const float* f = floatArgs.data();
    switch (id) {
    case 0x45:   // IsEqual
        EI_NAN_AUDIT(f[0], f[1]);
        *result = FromBool(f[0] == f[1]);
        return;
    case 0x46:   // IsLess
        EI_NAN_AUDIT(f[0], f[1]);
        *result = FromBool(f[0] < f[1]);
        return;
    case 0x47:   // IsGreater
        *result = FromBool(f[0] > f[1]);
        return;
    case 0x48:   // IsEqualString
        *result = FromBool(str::Compare(stringArgs[0], stringArgs[1]) == 0);
        return;
    case 0x49:   // Add
        *result = f[1] + f[0];
        return;
    case 0x4A:   // Sub
        *result = f[0] - f[1];
        return;
    case 0x4B:   // Mul
        *result = f[1] * f[0];
        return;
    case 0x4C:   // Div
        *result = f[0] / f[1];
        return;
    case 0x4D:   // Not
        EI_NAN_AUDIT(f[0]);
        *result = FromBool(f[0] == 0.0f);
        return;

    case 0x4F:   // HP
    case 0x50:   // MaxHP
    case 0x51:   // Mana
    case 0x52: { // MaxMana
        CEngineObject* o = objectArgs[0]->object;
        CMapObjectServer* stats = o ? ServerHalf(o) : nullptr;
        if (stats) {
            const UnitVitals& vitals = stats->vitals;
            const float* value = id == 0x4F ? &vitals.hitPoints
                               : id == 0x50 ? &vitals.maxHitPoints
                               : id == 0x51 ? &vitals.mana
                                            : &vitals.maxMana;
            x87::CopyBits(*result, *value);
        } else {
            *result = 0.0f;
        }
        return;
    }
    case 0x55:   // GetX
    case 0x56: { // GetY
        CEngineObject* o = objectArgs[0]->object;
        if (o) x87::CopyBits(*result, id == 0x55 ? ObjectSlots(o)->localPosition_.x : ObjectSlots(o)->localPosition_.y);
        else *result = 0.0f;
        return;
    }
    case 0x57: { // GetZ: height above the ground at the object's position
        CEngineObject* o = objectArgs[0]->object;
        if (!o) {
            *result = 0.0f;
            return;
        }
        float x = ObjectSlots(o)->localPosition_.x, y = ObjectSlots(o)->localPosition_.y, z = ObjectSlots(o)->localPosition_.z;
        if (CUnit* unit = UnitOrNull(o)) *result = static_cast<float>(UnitGroundHeight(unit, x, y) + z);   // (o is not null)
        else *result = g_serverWorld->GridHeight(x, y) + z;
        return;
    }
    case 0xE1:   // GetLootItemsCount
    case 0xE2: { // GetMoney
        *result = 0.0f;
        CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
        if (player) *result = static_cast<float>(id == 0xE1 ? std::ssize(player->items_) : player->Party()->money_);
        return;
    }
    case 0xE3: { // WasLooted
        *result = 1.0f;
        CEngineObject* o = objectArgs[0]->object;
        if (o) *result = static_cast<float>(lib::Has(o->flags_, MapObjectFlag::LootTaken));
        return;
    }
    case 0xBF:   // GetFutureX
    case 0xC0: { // GetFutureY
        CEngineObject* o = objectArgs[0]->object;
        if (!o) {
            *result = 0.0f;
            return;
        }
        Vector3 buffer;
        const Vector3* position = ObjectSlots(o)->PositionAt(&buffer, RoundTimeOffset(g_serverWorld->tick_, f[0]));
        x87::CopyBits(*result, *(id == 0xBF ? &position->x : &position->y));
        return;
    }
    case 0x99: { // IsUnitInWater: flag 0x80 of the AI map cell at the object's position
        CEngineObject* o = objectArgs[0]->object;
        int inWater = 0;
        if (o) inWater = g_serverWorld->aiMap_.CellFlag80At(&ObjectSlots(o)->localPosition_) ? 1 : 0;
        *result = static_cast<float>(inWater);
        return;
    }
    case 0x58:   // GroupSize
        *result = static_cast<float>(groupArgs[0]->size());
        return;
    case 0x5C: { // GroupHas
        CEngineObject* o = objectArgs[0]->object;
        *result = groupArgs[0]->Contains(o) ? 1.0f : 0.0f;
        return;
    }
    case 0x5F: { // Random(range): [0, range]
        float range = f[0];
        std::uint32_t value = g_random.Next();
        *result = ScaleRandom(value, range);
        return;
    }
    case 0x60: { // GetDiplomacy: 1 allied, -1 enemies, 0 otherwise
        int b = Ftol(f[1]);
        int a = Ftol(f[0]);
        int relation = g_serverWorld->diplomacy_.GetRelation(a, b);
        *result = 0.0f;
        if (relation == 2) *result = kMinusOne;
        else if (relation == 0) *result = 1.0f;
        return;
    }
    case 0x63:   // IsAlive
    case 0x64: { // IsDead
        CEngineObject* o = objectArgs[0]->object;
        CMapObjectCompanion* stats = o ? ObjectSlots(o)->companion : nullptr;
        *result = 0.0f;
        if (!stats) return;
        int state = stats->IsDead();
        *result = static_cast<float>(id == 0x63 ? (state == 0 ? 1 : 0) : state);
        return;
    }
    case 0x61: { // IsEnemy(unit, player)
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = 0.0f;
        if (!unit) return;
        int player = Ftol(f[0]);
        std::uint32_t mask = 1u << (player & 31);
        bool enemy = (mask & unit->relationsB & unit->relationsA) != 0;
        *result = static_cast<float>(enemy ? 1 : 0);
        return;
    }
    case 0x65: { // GetPlayer
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = 0.0f;
        if (unit) *result = static_cast<float>(unit->side);
        return;
    }
    case 0x8F: { // GetAIClass (the unit's size class)
        CUnit* unit = UnitOrNull(objectArgs[0]->object);
        *result = kMinusOne;
        if (unit) *result = static_cast<float>(unit->sizeClass);
        return;
    }
    case 0x90: { // GetLeverState
        CEngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o || o->GetClassId() != kClassLever) return;
        *result = static_cast<float>(static_cast<Lever*>(o)->state);
        return;
    }
    case 0x66: { // GetObjectID
        CEngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (o) *result = static_cast<float>(o->id_);
        return;
    }
    case 0x67:   // IsAlarm
        *result = FromBool(Alarm(x87::fistp(f[0])).active != 0);
        return;
    case 0x8B:   // AlarmTime
        *result = static_cast<float>(Alarm(x87::fistp(f[0])).time);
        return;
    case 0x8C:   // AlarmPosX
        x87::CopyBits(*result, Alarm(x87::fistp(f[0])).x);
        return;
    case 0x8D:   // AlarmPosY
        x87::CopyBits(*result, Alarm(x87::fistp(f[0])).y);
        return;
    case 0x53: { // DistanceUnitUnit
        CEngineObject* a = objectArgs[0]->object;
        CEngineObject* b = objectArgs[1]->object;
        if (a && b) *result = Distance(ObjectSlots(a)->localPosition_.x, ObjectSlots(a)->localPosition_.y, ObjectSlots(b)->localPosition_.x, ObjectSlots(b)->localPosition_.y);
        else *result = kFar;
        return;
    }
    case 0x54: { // DistanceUnitPoint
        CEngineObject* o = objectArgs[0]->object;
        if (o) *result = Distance(ObjectSlots(o)->localPosition_.x, ObjectSlots(o)->localPosition_.y, f[0], f[1]);
        else *result = kFar;
        return;
    }

    // Every/Any/Sum(var, group, condition): the condition (the nested float call) is evaluated
    // again for each member of the group with the object variable set to that member. Without a
    // nested call the condition is a constant.
    case 0x68:   // Every
    case 0x69:   // Any
    case 0x6A: { // Sum
        const ScriptSubCall* condition = nullptr;
        for (int i = 0; i < std::ssize(subCalls); ++i)
            if (subCalls[i].type == kScriptFloat) {
                condition = &subCalls[i];
                break;
            }
        if (!condition) {
            if (id == 0x68) x87::CopyBits(*result, floatArgs[0]);
            else if (id == 0x69) {
                EI_NAN_AUDIT(floatArgs[0]);
                *result = FromBool(floatArgs[0] != 0.0f);
            }
            else *result = static_cast<float>(groupArgs[0]->size()) * floatArgs[0];
            return;
        }
        *result = id == 0x68 ? 1.0f : 0.0f;
        ScriptGroup* g = groupArgs[0];
        if (g->empty()) return;
        int k = 0;
        do {
            *objectArgs[0] = (*g)[static_cast<std::size_t>(k)];
            CalcParameters(script, vars, condition);
            if (id == 0x68) {
                EI_NAN_AUDIT(floatArgs[0]);
                if (floatArgs[0] == 0.0f) {
                    *result = 0.0f;
                    return;
                }
            } else if (id == 0x69) {
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

    case 0x7C: { // HaveItem(player, item)
        *result = 0.0f;
        auto* player = g_serverWorld->GetPlayer(Ftol(f[0]));
        if (player && player->HasLoot(Ftol(f[1]))) *result = 1.0f;
        return;
    }
    case 0xB1:   // IsPlayerInDanger
    case 0xB2: { // IsPlayerInSafety
        *result = 0.0f;
        CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
        if (!player) return;
        int danger = player->Server()->refreshPending_;
        if (id == 0xB1 ? danger == 2 : danger == 0) *result = 1.0f;
        return;
    }
    case 0xD1:   // GetMercsNumber
        *result = static_cast<float>(g_serverWorld->CountLivingMercs(Ftol(f[0])));
        return;
    case 0x80: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        net::ServerSession* server = IsMultiplayerClient() ? ActiveServer() : nullptr;
        if (server) {
            std::string key = stringArgs[0];
            *result = static_cast<float>(server->GetVariable(&key));
            return;
        }
        *result = 0.0f;
        CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
        if (player) *result = player->Party()->variables_.Get(stringArgs[0]);
        return;
    }
    case 0xA2:   // IsCameraPlaying
        *result = static_cast<float>(static_cast<int>(GameCameraPathPlaying()));
        return;
    case 0xAA:   // IsNight (the world's day-time value, copied as a float)
        x87::CopyBits(*result, g_serverWorld->nightLevel_);
        return;
    case 0xB5:   // GetWorldTime
        *result = WorldTimeOfDay();
        return;
    case 0xBB: { // IsInSquare(x, y, x1, y1, x2, y2)
        EI_NAN_AUDIT(f[0], f[1], f[2], f[3], f[4], f[5]);
        bool inside = f[0] >= f[2] && f[0] <= f[4] && f[1] >= f[3] && f[1] <= f[5];
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xBC: { // UnitInSquare(object, x1, y1, x2, y2)
        CEngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o) return;
        float x = ObjectSlots(o)->localPosition_.x, y = ObjectSlots(o)->localPosition_.y;
        EI_NAN_AUDIT(x, y, f[0], f[1], f[2], f[3]);
        bool inside = x >= f[0] && x <= f[2] && y >= f[1] && y <= f[3];
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xE5: { // IsUnitVisible: in the local player's list of units always sent
        CEngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (!o) return;
        CPlayer* player = g_serverWorld->GetPlayer(0);
        if (!player) return;
        *result = static_cast<float>(player->Server()->partyUnits_.Contains(o) ? 1 : 0);
        return;
    }
    case 0xBD:   // GetZValue
        *result = g_serverWorld->GridHeight(f[0], f[1]);
        return;
    case 0xBE:   // GetBSZValue
        *result = static_cast<float>(g_serverWorld->RawSurfaceHeightAt(f[0], f[1]));
        return;
    case 0xC5: { // IsInArea(area, x, y)
        int area = x87::fistp(f[0]);
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
    case 0xE6: { // IsUnitBlocked
        CEngineObject* o = objectArgs[0]->object;
        *result = 0.0f;
        if (o) *result = static_cast<float>(lib::Has(o->flags_, MapObjectFlag::Blocked));
        return;
    }
    default:
        game::FatalError("Function does not return float at SSCCmd::CalcFunction");
        return;
    }
}

void SSCCmd::CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result) {
    PrepareArguments(script, vars);
    *result = script->NewTempObject();
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    switch (id) {
    case 0x4E: { // GetObject(id)
        CEngineObject* o = g_serverWorld->FindObject(Ftol(floatArgs[0]));
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", Ftol(floatArgs[0]), "GetObject");
        (*result)->object = o;
        return;
    }
    case 0xB4: { // GetUnitOfPlayer(player, index)
        CPlayer* player = g_serverWorld->GetPlayer(Ftol(floatArgs[0]));
        CEngineObject* o = nullptr;
        if (player) {
            const std::vector<PartyUnitRecord>& members = player->Party()->members_;
            int index = x87::fistp(floatArgs[1]);
            if (index >= 0 && index < std::ssize(members))
                o = g_serverWorld->FindUnit(static_cast<int>(members[index].unitId));
        }
        (*result)->object = o;
        return;
    }
    case 0x85: { // GetObjectByName
        CEngineObject* o = g_serverWorld->FindObject(TextKeyFromName(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with name %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByName");
        (*result)->object = o;
        return;
    }
    case 0xAB: { // GetObjectByID (decimal string)
        CEngineObject* o = g_serverWorld->FindObject(str::ToInt(stringArgs[0]));
        if (!o)
            game::DebugLog("Object with ID %s not found in function \"%s\".\n",
                           stringArgs[0], "GetObjectByID");
        (*result)->object = o;
        return;
    }
    case 0xE4: { // GetLeader: the server's leader in multiplayer, else player 0's first unit
        int unitId;
        if (net::ServerSession* server = ActiveServer()) {
            unitId = server->LeaderId();
        } else {
            unitId = 0x3B9AF5CE;
            CPlayer* player = g_serverWorld->GetPlayer(0);
            if (player && std::ssize(player->Party()->members_) > 0)
                unitId = static_cast<int>(player->Party()->members_[0].unitId);
        }
        (*result)->object = g_serverWorld->FindObject(unitId);
        return;
    }
    default:
        game::FatalError("Function does not return object at SSCCmd::CalcFunction");
        return;
    }
}

void SSCCmd::CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result) {
    ObjectListIterator objects;
    objects.manager = nullptr;
    PrepareArguments(script, vars);
    *result = &script->tempGroups_.emplace_back();
    ScriptGroup* group = *result;
    bool done = false;
    if (scriptaddon::Enabled()) {
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        done = scriptaddon::Dispatch(&call);
    }
    switch (done ? -1 : id) {
    case -1:
        break;
    case 0x59:   // GroupAdd
        group->Union(*groupArgs[0], *groupArgs[1]);
        break;
    case 0x5A:   // GroupSub
        group->Difference(*groupArgs[0], *groupArgs[1]);
        break;
    case 0x5B:   // GroupCross
        group->Intersection(*groupArgs[0], *groupArgs[1]);
        break;
    case 0x5D: { // UnitSee(unit)
        group->clear();
        CUnit* unit = objectArgs[0]->AsUnit();
        if (!unit) break;
        if (CUnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        break;
    }
    case 0x5E: { // GroupSee(group): what its units see
        group->clear();
        for (const ScriptObjectRef& member : *groupArgs[0]) {
            CUnit* unit = member.AsUnit();
            if (!unit) continue;
            if (CUnitServer* controller = unit->ServerController()) InsertSeen(*result, controller);
        }
        break;
    }
    case 0x62: { // GetPlayerUnits(player): the world's units whose player compares equal
        EI_NAN_AUDIT(floatArgs[0]);
        for (CEngineObject* o = objects.First(g_serverWorld->objectManager_); o; o = objects.Next()) {
            CUnit* unit = UnitOrNull(o);
            if (unit && static_cast<double>(unit->side) == floatArgs[0]) (*result)->Insert(o);
        }
        break;
    }
    case 0xA5: { // PlayerSee(player): the player's seen objects, its units and what they see
        CPlayer* player = g_serverWorld->GetPlayer(Ftol(floatArgs[0]));
        if (!player) {
            (*result)->clear();
            break;
        }
        **result = player->Server()->possessedGroup_;
        const auto& units = player->units_;
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
    if (objects.manager) --objects.manager->refCount_;   // the iterator's destructor
}
