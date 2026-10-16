// Evaluation of script functions: SSCCmd::CalcParameters, CalcFloat, CalcObject and CalcGroup. See
// script.h. Rebirth's AddNewScriptCommands dispatches to addon's functions first (script_addon.h).
//
// Floating point: the comparisons keep FCOM's status bits (x87::CompareStatus), so NaN arguments
// behave as in the original; the multi-step computations keep their intermediates unrounded
// (double locals) as the original keeps them in registers.
#include "script.h"
#include "script_addon.h"

#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"
#include "lever.h"
#include "map_object.h"
#include "net_classes.h"
#include "object.h"
#include "object_diplomacy.h"
#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "random.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_script.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// ---- x87 helpers --------------------------------------------------------------------------------

// FCOM status bits (from FNSTSW AX, in AH): C0 = 0x01 "less or unordered", C3 = 0x40 "equal or
// unordered". The original tests these directly, so NaN arguments behave like the original.
constexpr int kLess = x87::kC0;
constexpr int kEqual = x87::kC3;
constexpr int kLessOrEqual = x87::kC0 | x87::kC3;

int Compare(float a, float b) { return x87::CompareStatus(a, b); }

// sqrt(dy * dy + dx * dx) with dx = x0 - x1 and dy = y0 - y1 kept unrounded (in registers).
float Distance(float x0, float y0, float x1, float y1) {
    const double dx = static_cast<double>(x0) - x1;
    const double dy = static_cast<double>(y0) - y1;
    return static_cast<float>(x87::fsqrt(LengthSquared2DUnrounded(dy, dx)));
}

// FCOM status of (dy * dy + dx * dx) against r2, as IsInArea tests circles.
int CompareSquaredDistance(float x, float y, float centreX, float centreY, float r2) {
    const double dx = static_cast<double>(x) - centreX;
    const double dy = static_cast<double>(y) - centreY;
    return x87::CompareStatus(LengthSquared2DUnrounded(dy, dx), r2);
}

// 0073C468: 1 / (2^32 - 1).
const std::uint64_t kRandomScaleBits = 0x3DF0000000100000ull;

// value * range / (2^32 - 1): the 32-bit value loaded as a 64-bit integer, two multiplications.
float ScaleRandom(std::uint32_t value, float range) {
    return static_cast<float>(x87::FromUnsigned(value) * range * std::bit_cast<double>(kRandomScaleBits));
}

// FISTP((float)(time + add)): the sum is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    return x87::fistp(static_cast<float>(static_cast<double>(time) + extra));
}

// The game's _ftol (006E853C): truncating conversion; out-of-range values keep the low 32 bits
// of the 64-bit result.
int Ftol(float value) { return static_cast<int>(gamecrt::Ftol(value)); }

// Copies a float through the FPU (FLD/FSTP), like the original.
void CopyFloat(float* dst, const float* src) { x87::CopyFloat(dst, src); }

void StoreBits(float* dst, std::uint32_t bits) { std::memcpy(dst, &bits, 4); }
constexpr std::uint32_t kZero = 0;
constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kMinusOne = 0xBF800000;
constexpr std::uint32_t kFar = 0x7149F2CA;     // 1e30: distance when an object is missing

// (float)(int)b: the original converts 0/1 with FILD.
float FromBool(bool b) { return static_cast<float>(b ? 1 : 0); }

// ---- Game state reached by the functions --------------------------------------------------------

constexpr int kClassLever = 0x5D;

// A script object as the map object it is (units, levers, ...): position at +0x1C, the
// side-specific companion at +0x170.
CMapObject* MapObject(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// The companion holding a map object's vitals. Scripts run on the server, so it is the
// CMapObjectServer (the original reads it without checking).
CMapObjectServer* ServerHalf(CEngineObject* object) {
    return static_cast<CMapObjectServer*>(MapObject(object)->companion);
}

// 00530280: the ground (or water surface) height under (x, y) for the unit; ST0 stays unrounded,
// as the original left it.
double UnitGroundHeight(CEngineObject* unit, float x, float y) { return UnitGroundHeightOriginal(unit, nullptr, x, y); }

// 00665640 (RET 4): CPlayer::HasLoot, called through its address.
__declspec(naked) BOOL __fastcall PlayerHasLoot(CPlayer*, void*, int) EI_ORIGINAL(0x00665640)

// The game server session when one runs and is not shut down.
net::ServerSession* ActiveServer() {
    net::ServerSession* server = net::GameServer();
    if (server && !(server->flags7_ & CRefCounted::kDetached)) return server;
    return nullptr;
}

// The local player's client glue (007AFEA5; the object is not read).
struct ClientGlue {
    int IsCameraPlaying();   // 005788B0
};
__declspec(naked) int ClientGlue::IsCameraPlaying() EI_ORIGINAL(0x005788B0)

WorldAlarm& Alarm(int index) { return WorldServer()->alarms_[index]; }

int WorldTick() { return WorldServer()->tick_; }

// _CIfmod (006E95AA): ST1 mod ST0. GetWorldTime: (time * scale + offset) mod 24.
float WorldTimeOfDay() {
    const CWorldServer* world = WorldServer();
    return static_cast<float>(libcrt::CiFmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, 24.0));
}

CEngineObject* AsUnit(CEngineObject* o) { return o && o->GetClassId() == kClassUnit ? o : nullptr; }

// Release a slot's object and store `object` with a new reference.
void Store(ScriptObjectRef* slot, CEngineObject* object) {
    if (slot->object) slot->object->Release();
    slot->object = nullptr;
    if (object) ++object->refCount_;
    slot->object = object;
}

} // namespace

// ---- CScript forwarders -------------------------------------------------------------------------


// ---- SSCCmd -------------------------------------------------------------------------------------

void SSCCmd::PrepareArguments(CScript* script, ScriptVars* vars) {
    SetParameters(&localBindings, vars);
    SetParameters(&globalBindings, script->Globals());
    for (int i = 0; i < subCalls.size; ++i) CalcParameters(script, vars, &subCalls.data[i]);
}

void SSCCmd::CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call) {
    switch (call->type) {
    case kScriptFloat:
        call->cmd->CalcFloat(script, vars, &floatArgs.data[call->argIndex]);
        return;
    case kScriptString: {
        // Rebirth's AddNewScriptCommands (004B4066) calls SSCCmd::CalcString (004B5650), which
        // runs addon's string functions.
        if (scriptaddon::Enabled()) {
            scriptaddon::CalcString(call->cmd, script, vars, &stringArgs.data[call->argIndex]);
            return;
        }
        // No function returns a string: the arguments are still evaluated before the error.
        call->cmd->PrepareArguments(script, vars);
        game::FatalError("Function does not return string at SSCCmd::CalcFunction");
        return;
    }
    case kScriptObject:
        call->cmd->CalcObject(script, vars, &objectArgs.data[call->argIndex]);
        return;
    case kScriptGroup:
        call->cmd->CalcGroup(script, vars, &groupArgs.data[call->argIndex]);
        return;
    default:
        game::FatalError("SSCCmd::CalcParameters, unknown function parameter type");
    }
}
EI_REPLACE_METHOD(0x004B4030, SSCCmd::CalcParameters);

void SSCCmd::CalcFloat(CScript* script, ScriptVars* vars, float* result) {
    PrepareArguments(script, vars);
    if (scriptaddon::Enabled()) {   // AddNewScriptCommands (hook at 004B425A)
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    const float* f = floatArgs.data;
    switch (id) {
    case 0x45:   // IsEqual
        *result = FromBool((Compare(f[0], f[1]) & kEqual) != 0);
        return;
    case 0x46:   // IsLess
        *result = FromBool((Compare(f[0], f[1]) & kLess) != 0);
        return;
    case 0x47:   // IsGreater
        *result = FromBool((Compare(f[0], f[1]) & kLessOrEqual) == 0);
        return;
    case 0x48:   // IsEqualString
        *result = FromBool(GameMbscmp(stringArgs.data[0], stringArgs.data[1]) == 0);
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
        *result = FromBool((Compare(f[0], 0.0f) & kEqual) != 0);
        return;

    case 0x4F:   // HP
    case 0x50:   // MaxHP
    case 0x51:   // Mana
    case 0x52: { // MaxMana
        CEngineObject* o = objectArgs.data[0]->object;
        CMapObjectServer* stats = o ? ServerHalf(o) : nullptr;
        if (stats) {
            const UnitVitals& vitals = stats->vitals;
            const float* value = id == 0x4F ? &vitals.hitPoints
                               : id == 0x50 ? &vitals.maxHitPoints
                               : id == 0x51 ? &vitals.mana
                                            : &vitals.maxMana;
            CopyFloat(result, value);
        } else {
            StoreBits(result, kZero);
        }
        return;
    }
    case 0x55:   // GetX
    case 0x56: { // GetY
        CEngineObject* o = objectArgs.data[0]->object;
        if (o) CopyFloat(result, id == 0x55 ? &MapObject(o)->position.x : &MapObject(o)->position.y);
        else StoreBits(result, kZero);
        return;
    }
    case 0x57: { // GetZ: height above the ground at the object's position
        CEngineObject* o = objectArgs.data[0]->object;
        if (!o) {
            StoreBits(result, kZero);
            return;
        }
        float x = MapObject(o)->position.x, y = MapObject(o)->position.y, z = MapObject(o)->position.z;
        if (o->GetClassId() == kClassUnit) *result = static_cast<float>(UnitGroundHeight(o, x, y) + z);
        else *result = WorldServer()->GridHeight(x, y) + z;
        return;
    }
    case 0xE1:   // GetLootItemsCount
    case 0xE2: { // GetMoney
        StoreBits(result, kZero);
        WorldPlayer* player = WorldServer()->GetPlayer(Ftol(f[0]));
        if (player) *result = static_cast<float>(id == 0xE1 ? player->items.count : player->Party()->Money());
        return;
    }
    case 0xE3: { // WasLooted
        StoreBits(result, kOne);
        CEngineObject* o = objectArgs.data[0]->object;
        if (o) *result = static_cast<float>(static_cast<int>((o->flags_ >> 20) & 1));
        return;
    }
    case 0xBF:   // GetFutureX
    case 0xC0: { // GetFutureY
        CEngineObject* o = objectArgs.data[0]->object;
        if (!o) {
            StoreBits(result, kZero);
            return;
        }
        Vector3 buffer;
        const Vector3* position = ObjectSlots(o)->PositionAt(&buffer, RoundTimeOffset(WorldTick(), f[0]));
        std::memcpy(result, id == 0xBF ? &position->x : &position->y, 4);
        return;
    }
    case 0x99: { // IsUnitInWater: flag 0x80 of the AI map cell at the object's position
        CEngineObject* o = objectArgs.data[0]->object;
        int inWater = 0;
        if (o) inWater = WorldServer()->AIMap()->CellFlag80At(&MapObject(o)->position) ? 1 : 0;
        *result = static_cast<float>(inWater);
        return;
    }
    case 0x58:   // GroupSize
        *result = static_cast<float>(groupArgs.data[0]->size);
        return;
    case 0x5C: { // GroupHas
        CEngineObject* o = objectArgs.data[0]->object;
        const ScriptGroup* g = groupArgs.data[0];
        bool found = false;
        for (int i = 0; i < g->size; ++i)
            if (g->data[i].object == o) {
                found = true;
                break;
            }
        StoreBits(result, found ? kOne : kZero);
        return;
    }
    case 0x5F: { // Random(range): [0, range]
        float range = f[0];
        std::uint32_t value = IsaacRandom::Global().Next();
        *result = ScaleRandom(value, range);
        return;
    }
    case 0x60: { // GetDiplomacy: 1 allied, -1 enemies, 0 otherwise
        int b = Ftol(f[1]);
        int a = Ftol(f[0]);
        int relation = GameDiplomacy()->GetRelation(a, b);
        StoreBits(result, kZero);
        if (relation == 2) StoreBits(result, kMinusOne);
        else if (relation == 0) StoreBits(result, kOne);
        return;
    }
    case 0x63:   // IsAlive
    case 0x64: { // IsDead
        CEngineObject* o = objectArgs.data[0]->object;
        CEngineObject* stats = o ? MapObject(o)->companion : nullptr;
        StoreBits(result, kZero);
        if (!stats) return;
        int state = static_cast<CMapObjectCompanion*>(stats)->IsDead();
        *result = static_cast<float>(id == 0x63 ? (state == 0 ? 1 : 0) : state);
        return;
    }
    case 0x61: { // IsEnemy(unit, player)
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kZero);
        if (!unit) return;
        int player = Ftol(f[0]);
        std::uint32_t mask = 1u << (player & 31);
        const CUnit* u = static_cast<const CUnit*>(unit);
        bool enemy = (mask & u->relationsB & u->relationsA) != 0;
        *result = static_cast<float>(enemy ? 1 : 0);
        return;
    }
    case 0x65: { // GetPlayer
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kZero);
        if (unit) *result = static_cast<float>(static_cast<CUnit*>(unit)->side);
        return;
    }
    case 0x8F: { // GetAIClass (the unit's size class, +0x324)
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kMinusOne);
        if (unit) *result = static_cast<float>(static_cast<CUnit*>(unit)->sizeClass);
        return;
    }
    case 0x90: { // GetLeverState
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (!o || o->GetClassId() != kClassLever) return;
        *result = static_cast<float>(static_cast<Lever*>(o)->state);
        return;
    }
    case 0x66: { // GetObjectID
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
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
        std::memcpy(result, &Alarm(x87::fistp(f[0])).x, 4);
        return;
    case 0x8D:   // AlarmPosY
        std::memcpy(result, &Alarm(x87::fistp(f[0])).y, 4);
        return;
    case 0x53: { // DistanceUnitUnit
        CEngineObject* a = objectArgs.data[0]->object;
        CEngineObject* b = objectArgs.data[1]->object;
        if (a && b) *result = Distance(MapObject(a)->position.x, MapObject(a)->position.y, MapObject(b)->position.x, MapObject(b)->position.y);
        else StoreBits(result, kFar);
        return;
    }
    case 0x54: { // DistanceUnitPoint
        CEngineObject* o = objectArgs.data[0]->object;
        if (o) *result = Distance(MapObject(o)->position.x, MapObject(o)->position.y, f[0], f[1]);
        else StoreBits(result, kFar);
        return;
    }

    // Every/Any/Sum(var, group, condition): the condition (the nested float call) is evaluated
    // again for each member of the group with the object variable set to that member. Without a
    // nested call the condition is a constant.
    case 0x68:   // Every
    case 0x69:   // Any
    case 0x6A: { // Sum
        const ScriptSubCall* condition = nullptr;
        for (int i = 0; i < subCalls.size; ++i)
            if (subCalls.data[i].type == kScriptFloat) {
                condition = &subCalls.data[i];
                break;
            }
        if (!condition) {
            if (id == 0x68) std::memcpy(result, &floatArgs.data[0], 4);
            else if (id == 0x69) *result = FromBool((Compare(floatArgs.data[0], 0.0f) & kEqual) == 0);
            else *result = static_cast<float>(groupArgs.data[0]->size) * floatArgs.data[0];
            return;
        }
        StoreBits(result, id == 0x68 ? kOne : kZero);
        ScriptGroup* g = groupArgs.data[0];
        if (g->size <= 0) return;
        int k = 0;
        do {
            ScriptObjectRef* member = &g->data[k];
            ScriptObjectRef* variable = objectArgs.data[0];
            if (variable->object) variable->object->Release();
            variable->object = nullptr;
            if (id == 0x6A) {
                CEngineObject* o = member->object;
                variable->object = o;
                if (o) ++o->refCount_;
            } else {
                variable->Construct(*member);
            }
            CalcParameters(script, vars, condition);
            if (id == 0x68) {
                if (Compare(floatArgs.data[0], 0.0f) & kEqual) {
                    StoreBits(result, kZero);
                    return;
                }
            } else if (id == 0x69) {
                if (!(Compare(floatArgs.data[0], 0.0f) & kEqual)) {
                    StoreBits(result, kOne);
                    return;
                }
            } else {
                *result = floatArgs.data[0] + *result;
            }
            ++k;
            g = groupArgs.data[0];
        } while (k < g->size);
        return;
    }

    case 0x7C: { // HaveItem(player, item)
        StoreBits(result, kZero);
        auto* player = reinterpret_cast<CPlayer*>(WorldServer()->GetPlayer(Ftol(f[0])));
        if (player && PlayerHasLoot(player, nullptr, Ftol(f[1]))) StoreBits(result, kOne);
        return;
    }
    case 0xB1:   // IsPlayerInDanger
    case 0xB2: { // IsPlayerInSafety
        StoreBits(result, kZero);
        WorldPlayer* player = WorldServer()->GetPlayer(Ftol(f[0]));
        if (!player) return;
        int danger = player->server->refreshPending_;
        if (id == 0xB1 ? danger == 2 : danger == 0) StoreBits(result, kOne);
        return;
    }
    case 0xD1:   // GetMercsNumber
        *result = static_cast<float>(WorldServer()->CountLivingMercs(Ftol(f[0])));
        return;
    case 0x80: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        net::ServerSession* server = IsNetworkClientOriginal() ? ActiveServer() : nullptr;
        if (server) {
            const char* name = stringArgs.data[0];
            stlp::string key(name);
            // StringRep is the raw layout of an STLport string.
            *result = static_cast<float>(server->GetVariable(reinterpret_cast<const net::StringRep*>(&key)));
            return;
        }
        StoreBits(result, kZero);
        WorldPlayer* player = WorldServer()->GetPlayer(Ftol(f[0]));
        if (player) *result = NamedTableGet(player->variables, nullptr, stringArgs.data[0]);
        return;
    }
    case 0xA2:   // IsCameraPlaying
        *result = static_cast<float>(static_cast<ClientGlue*>(ClientGlueObject())->IsCameraPlaying());
        return;
    case 0xAA:   // IsNight (the world's day-time value, copied as a float)
        CopyFloat(result, reinterpret_cast<const float*>(&WorldServer()->nightLevel_));
        return;
    case 0xB5:   // GetWorldTime
        *result = WorldTimeOfDay();
        return;
    case 0xBB: { // IsInSquare(x, y, x1, y1, x2, y2)
        bool inside = !(Compare(f[0], f[2]) & kLess) && (Compare(f[0], f[4]) & kLessOrEqual) &&
                      !(Compare(f[1], f[3]) & kLess) && (Compare(f[1], f[5]) & kLessOrEqual);
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xBC: { // UnitInSquare(object, x1, y1, x2, y2)
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (!o) return;
        float x = MapObject(o)->position.x, y = MapObject(o)->position.y;
        bool inside = !(Compare(x, f[0]) & kLess) && (Compare(x, f[2]) & kLessOrEqual) &&
                      !(Compare(y, f[1]) & kLess) && (Compare(y, f[3]) & kLessOrEqual);
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xE5: { // IsUnitVisible: in the local player's list of units always sent
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (!o) return;
        WorldPlayer* player = WorldServer()->GetPlayer(0);
        if (!player) return;
        const ScriptGroup& visible = player->server->partyUnits_;
        bool found = false;
        for (int i = 0; i < visible.size; ++i)
            if (visible.data[i].object == o) {
                found = true;
                break;
            }
        *result = static_cast<float>(found ? 1 : 0);
        return;
    }
    case 0xBD:   // GetZValue
        *result = WorldServer()->GridHeight(f[0], f[1]);
        return;
    case 0xBE:   // GetBSZValue
        *result = static_cast<float>(WorldServer()->RawSurfaceHeightAt(f[0], f[1]));
        return;
    case 0xC5: { // IsInArea(area, x, y)
        int area = x87::fistp(f[0]);
        float x = f[1], y = f[2];
        const mfc::CArray<ScriptAreaPart>* parts = script->FindArea(area);
        bool inside = false;
        for (int i = 0, n = parts->size; i < n; ++i) {
            const ScriptAreaPart& part = parts->data[i];
            if (part.isRect) {
                if (Compare(x, part.x) & kLess) continue;
                if (!(Compare(x, part.x2OrRadiusSquared) & kLessOrEqual)) continue;
                if (Compare(y, part.y) & kLess) continue;
                if (Compare(y, part.y2) & kLessOrEqual) {
                    inside = true;
                    break;
                }
            } else if (CompareSquaredDistance(x, y, part.x, part.y, part.x2OrRadiusSquared) & kLess) {
                inside = true;
                break;
            }
        }
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xE6: { // IsUnitBlocked
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (o) *result = static_cast<float>(static_cast<int>((o->flags_ >> 23) & 1));
        return;
    }
    default:
        game::FatalError("Function does not return float at SSCCmd::CalcFunction");
        return;
    }
}
EI_REPLACE_METHOD(0x004B41E0, SSCCmd::CalcFloat);

void SSCCmd::CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result) {
    PrepareArguments(script, vars);
    *result = script->NewTempObject();
    if (scriptaddon::Enabled()) {   // AddNewScriptCommands (hook at 004B572F)
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    switch (id) {
    case 0x4E: { // GetObject(id)
        CEngineObject* o = WorldServer()->FindObject(Ftol(floatArgs.data[0]));
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", Ftol(floatArgs.data[0]), "GetObject");
        ScriptObjectRef* slot = *result;
        slot->Reset();
        if (o) ++o->refCount_;
        slot->object = o;
        return;
    }
    case 0xB4: { // GetUnitOfPlayer(player, index)
        WorldPlayer* player = WorldServer()->GetPlayer(Ftol(floatArgs.data[0]));
        CEngineObject* o = nullptr;
        if (player) {
            const PartyMemberArray& members = player->Party()->members_;
            int index = x87::fistp(floatArgs.data[1]);
            if (index >= 0 && index < members.size)
                o = WorldFindUnit(WorldServer(), nullptr, static_cast<int>(members.data[index].unitId));
        }
        Store(*result, o);
        return;
    }
    case 0x85: { // GetObjectByName
        CEngineObject* o = WorldServer()->FindObject(TextKeyFromName(stringArgs.data[0]));
        if (!o)
            game::DebugLog("Object with name %s not found in function \"%s\".\n",
                           static_cast<const char*>(stringArgs.data[0]), "GetObjectByName");
        Store(*result, o);
        return;
    }
    case 0xAB: { // GetObjectByID (decimal string)
        CEngineObject* o = WorldServer()->FindObject(GameAtoi(stringArgs.data[0]));
        if (!o)
            game::DebugLog("Object with ID %s not found in function \"%s\".\n",
                           static_cast<const char*>(stringArgs.data[0]), "GetObjectByID");
        Store(*result, o);
        return;
    }
    case 0xE4: { // GetLeader: the server's leader in multiplayer, else player 0's first unit
        int unitId;
        if (net::ServerSession* server = ActiveServer()) {
            unitId = server->LeaderId();
        } else {
            unitId = 0x3B9AF5CE;
            WorldPlayer* player = WorldServer()->GetPlayer(0);
            if (player && player->Party()->members_.size > 0)
                unitId = static_cast<int>(player->Party()->members_.data[0].unitId);
        }
        Store(*result, WorldServer()->FindObject(unitId));
        return;
    }
    default:
        game::FatalError("Function does not return object at SSCCmd::CalcFunction");
        return;
    }
}
EI_REPLACE_METHOD(0x004B56C0, SSCCmd::CalcObject);

// ---- CalcGroup ----------------------------------------------------------------------------------

namespace {

using TempGroupList = mfc::CList<ScriptGroup>;

// The script's temporary groups: AddTail (004C8D80) copies the group into a new node and returns
// the node. 004C2290: group destructor. 004C1290: group assignment.
__declspec(naked) TempGroupList::Node* __fastcall GroupTempsAddTail(TempGroupList*, void*, const ScriptGroup*) EI_ORIGINAL(0x004C8D80)
inline void __fastcall GroupDestruct(ScriptGroup* a0) { ScriptGroupDestructOriginal((void*)a0); }
inline void __fastcall GroupAssign(ScriptGroup* a0, void*, const ScriptGroup* a2) { GroupAssignOriginal((void*)a0, nullptr, (const void*)a2); }

// FNSTSW after FILD a, FCOMP b (AH).
int CompareIntFloat(int a, float b) { return x87::CompareStatus(static_cast<double>(a), b); }

// What a unit sees: the units in sight of its server half (CUnit::controller, the AI brain's
// visible list).
void InsertSeen(ScriptGroup* group, CEngineObject* controller) {
    const auto& visible = static_cast<CUnitServer*>(controller)->Brain()->visible;
    for (auto* node = visible.head; node;) {
        auto* current = node;
        node = current->next;
        group->Insert(current->data);
    }
}

CEngineObject* ControllerOf(CEngineObject* unit) { return static_cast<CUnit*>(unit)->controller; }

} // namespace

void SSCCmd::CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result) {
    ObjectListIterator objects;
    objects.manager = nullptr;
    PrepareArguments(script, vars);
    {
        ScriptGroup empty;   // 004B5ED0
        empty.vtable = scriptvtable::kRefs;
        empty.data = nullptr;
        empty.size = 0;
        empty.maxSize = 0;
        empty.growBy = 0;
        TempGroupList::Node* node = GroupTempsAddTail(&script->tempGroups_, nullptr, &empty);
        GroupDestruct(&empty);
        *result = &node->data;
    }
    ScriptGroup* group = *result;
    bool done = false;
    if (scriptaddon::Enabled()) {   // AddNewScriptCommands (hook at 004B5BAF)
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        done = scriptaddon::Dispatch(&call);
    }
    switch (done ? -1 : id) {
    case -1:
        break;
    case 0x59:   // GroupAdd
        group->Union(*groupArgs.data[0], *groupArgs.data[1]);
        break;
    case 0x5A:   // GroupSub
        group->Difference(*groupArgs.data[0], *groupArgs.data[1]);
        break;
    case 0x5B:   // GroupCross
        group->Intersection(*groupArgs.data[0], *groupArgs.data[1]);
        break;
    case 0x5D: { // UnitSee(unit)
        group->SetSize(0, -1);
        CEngineObject* unit = objectArgs.data[0]->AsUnit();
        if (!unit) break;
        if (CEngineObject* controller = ControllerOf(unit)) InsertSeen(*result, controller);
        break;
    }
    case 0x5E: { // GroupSee(group): what its units see
        group->SetSize(0, -1);
        for (int i = 0; i < groupArgs.data[0]->size; ++i) {
            CEngineObject* unit = groupArgs.data[0]->data[i].AsUnit();
            if (!unit) continue;
            if (CEngineObject* controller = ControllerOf(unit)) InsertSeen(*result, controller);
        }
        break;
    }
    case 0x62: { // GetPlayerUnits(player): the world's units whose player compares equal (or NaN)
        for (CEngineObject* o = objects.First(WorldObjectManager()); o; o = objects.Next()) {
            if (o->GetClassId() != kClassUnit) continue;
            if (CompareIntFloat(static_cast<CUnit*>(o)->side, floatArgs.data[0]) & kEqual) (*result)->Insert(o);
        }
        break;
    }
    case 0xA5: { // PlayerSee(player): the player's seen objects, its units and what they see
        WorldPlayer* player = WorldServer()->GetPlayer(Ftol(floatArgs.data[0]));
        if (!player) {
            (*result)->SetSize(0, -1);
            break;
        }
        GroupAssign(*result, nullptr, &player->server->group50F8_);
        const auto& units = player->units.map;
        for (auto* node = units.First(); node; node = units.After(node)) {
            CEngineObject* unit = node->value;
            if (!unit) break;
            (*result)->Insert(unit);
            InsertSeen(*result, ControllerOf(unit));   // no null check, as in the original
        }
        break;
    }
    default:
        game::FatalError("Function does not return string at SSCCmd::CalcFunction");
        break;
    }
    if (objects.manager) --objects.manager->refCount_;   // the iterator's destructor
}
EI_REPLACE_METHOD(0x004B5B00, SSCCmd::CalcGroup);
