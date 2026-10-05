// Evaluation of script functions: SSCCmd::CalcParameters, CalcFloat, CalcObject and CalcGroup. See
// script.h. Rebirth's AddNewScriptCommands dispatches to addon's functions first (script_addon.h).
//
// Floating point: the comparisons and the few multi-step computations are written with the
// original's x87 instruction sequences (helpers below), because their results depend on 80-bit
// intermediates and on how FCOM reports NaN. Single operations stored straight to a float (a + b,
// a / b, int to float) are plain C++.
#include "script.h"
#include "script_addon.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "random.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

namespace {

// ---- x87 helpers --------------------------------------------------------------------------------

// FCOM status bits (from FNSTSW AX, in AH): C0 = 0x01 "less or unordered", C3 = 0x40 "equal or
// unordered". The original tests these directly, so NaN arguments behave like the original.
constexpr int kLess = 0x01;
constexpr int kEqual = 0x40;
constexpr int kLessOrEqual = 0x41;

int Compare(float a, float b) {
    int status;
    __asm {
        fld a
        fcomp b
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return status;
}

// sqrt(dy * dy + dx * dx) with dx = x0 - x1 and dy = y0 - y1 kept in 80-bit registers.
float Distance(float x0, float y0, float x1, float y1) {
    float result;
    __asm {
        fld x0
        fsub x1
        fld y0
        fsub y1
        fld st(0)
        fmul st, st(1)
        fld st(2)
        fmul st, st(3)
        faddp st(1), st
        fsqrt
        fstp result
        fstp st(0)
        fstp st(0)
    }
    return result;
}

// FCOM status of (dy * dy + dx * dx) against r2, as IsInArea tests circles.
int CompareSquaredDistance(float x, float y, float centreX, float centreY, float r2) {
    int status;
    __asm {
        fld x
        fsub centreX
        fld y
        fsub centreY
        fld st(0)
        fmul st, st(1)
        fld st(2)
        fmul st, st(3)
        faddp st(1), st
        fcomp r2
        fnstsw ax
        fstp st(0)
        fstp st(0)
        movzx eax, ah
        mov status, eax
    }
    return status;
}

// 0073C468: 1 / (2^32 - 1).
const std::uint64_t kRandomScaleBits = 0x3DF0000000100000ull;

// value * range / (2^32 - 1): the 32-bit value loaded as a 64-bit integer, two multiplications.
float ScaleRandom(std::uint32_t value, float range) {
    std::uint32_t wide[2] = {value, 0};
    const std::uint64_t* scale = &kRandomScaleBits;
    float result;
    __asm {
        fild qword ptr [wide]
        fmul range
        mov eax, scale
        fmul qword ptr [eax]
        fstp result
    }
    return result;
}

// FISTP((float)(time + add)): the sum is rounded to float before the conversion.
int RoundTimeOffset(int time, float extra) {
    float sum;
    int result;
    __asm {
        fild time
        fadd extra
        fstp sum
        fld sum
        fistp result
    }
    return result;
}

// The game's _ftol (006E853C): truncating conversion; out-of-range values keep the low 32 bits
// of the 64-bit result.
__declspec(naked) int __cdecl Ftol(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// Copies a float through the FPU (FLD/FSTP), like the original.
void CopyFloat(float* dst, const float* src) {
    __asm {
        mov eax, src
        fld dword ptr [eax]
        mov eax, dst
        fstp dword ptr [eax]
    }
}

void StoreBits(float* dst, std::uint32_t bits) { std::memcpy(dst, &bits, 4); }
constexpr std::uint32_t kZero = 0;
constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kMinusOne = 0xBF800000;
constexpr std::uint32_t kFar = 0x7149F2CA;     // 1e30: distance when an object is missing

// (float)(int)b: the original converts 0/1 with FILD.
float FromBool(bool b) { return static_cast<float>(b ? 1 : 0); }

// ---- Game state reached by the functions --------------------------------------------------------

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kClassUnit = 0x50;
constexpr int kClassLever = 0x5D;

// The world (global at 007AFF60).
struct World {
    CEngineObject* FindObject(int id);                // 00593AA0
    void* GetPlayer(int index);                        // 00594180
    float TerrainHeight(float x, float y);             // 00594220
    int MercsNumber(int player);                       // 00591740
    CEngineObject* FindUnitObject(int unitId);         // 0055FB00
};
__declspec(naked) CEngineObject* World::FindObject(int) EI_ORIGINAL(0x00593AA0)
void* World::GetPlayer(int id) { return ::WorldGetPlayer(this, nullptr, id); }
__declspec(naked) float World::TerrainHeight(float, float) EI_ORIGINAL(0x00594220)
__declspec(naked) int World::MercsNumber(int) EI_ORIGINAL(0x00591740)
CEngineObject* World::FindUnitObject(int id) { return ::WorldFindUnit(this, nullptr, id); }
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

// Height of the ground under a unit (00530280, a unit method).
struct UnitGround {
    float Height(float x, float y);
};
__declspec(naked) float UnitGround::Height(float, float) EI_ORIGINAL(0x00530280)

// Diplomacy table (007C2BC4).
struct Diplomacy {
    int Get(int a, int b);   // 00455630: 0 allied, 2 enemies
};
__declspec(naked) int Diplomacy::Get(int, int) EI_ORIGINAL(0x00455630)

struct Player {
    int HasItem(int item);   // 00665640
};
__declspec(naked) int Player::HasItem(int) EI_ORIGINAL(0x00665640)

// Per-player quest variables (player + 0xC4).
struct QuestVars {
    float Get(const char* name);   // 00460AF0
};
__declspec(naked) float QuestVars::Get(const char*) EI_ORIGINAL(0x00460AF0)

// Water level map (007B00D0).
struct WaterMap {
    bool IsInWater(const float* position);   // 005BBC70
};
__declspec(naked) bool WaterMap::IsInWater(const float*) EI_ORIGINAL(0x005BBC70)

// Network state (007AFE20) and the game server object (pointer at 0079B5DC).
struct Network {
    bool IsMultiplayer();   // 0057F440
};
bool Network::IsMultiplayer() { return IsNetworkClientOriginal(); }

struct GameServer {
    float GetVar(const stlp::string* name);   // 00419940
    int GetLeaderId();                         // 0041A020
};
__declspec(naked) float GameServer::GetVar(const stlp::string*) EI_ORIGINAL(0x00419940)
__declspec(naked) int GameServer::GetLeaderId() EI_ORIGINAL(0x0041A020)

GameServer* ActiveServer() {
    auto* server = *reinterpret_cast<GameServer**>(0x0079B5DC);
    if (server && !(Field<std::uint32_t>(server, 4) & 0x80000000u)) return server;
    return nullptr;
}

// Camera script player (007AFEA5).
struct CameraPlayer {
    int IsPlaying();   // 005788B0
};
__declspec(naked) int CameraPlayer::IsPlaying() EI_ORIGINAL(0x005788B0)

// Alarms raised by InvokeAlarm: 16-byte slots at 007AFFEC.
struct AlarmSlot {
    std::uint8_t active;
    std::uint8_t unknown_01[3];
    int time;
    float x;
    float y;
};
static_assert(sizeof(AlarmSlot) == 16);
AlarmSlot& Alarm(int index) { return reinterpret_cast<AlarmSlot*>(0x007AFFEC)[index]; }

int GameTime() { return *reinterpret_cast<int*>(0x007AFFC4); }

// _CIfmod (006E95AA): ST1 mod ST0. GetWorldTime: (time * scale + offset) mod 24.
float WorldTimeOfDay() {
    const double day = 24.0;   // 0073D108
    float result;
    __asm {
        fild dword ptr ds:[0x007AFFC4]
        fmul dword ptr ds:[0x007AFF80]
        fadd dword ptr ds:[0x007AFF7C]
        fld day
        mov eax, 0x006E95AA
        call eax
        fstp result
    }
    return result;
}

// Object id from a name (00461360): (sum * 5 + toupper(c)) % 1e9 + 1e9, ECX = name.
__declspec(naked) int __fastcall ObjectIdFromName(const char*) EI_ORIGINAL(0x00461360)
__declspec(naked) int __cdecl GameMbscmp(const char*, const char*) EI_ORIGINAL(0x006E848B)

// Virtual calls on world objects by vtable offset.
const float* FuturePosition(CEngineObject* object, float* buffer, int time) {
    using Fn = const float*(__thiscall*)(CEngineObject*, float*, int);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[0xD0 / 4])(object, buffer, time);
}
int DeathState(void* stats) {
    using Fn = int(__thiscall*)(void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(stats))[0x44 / 4])(stats);
}
float WorldBszValue(float x, float y) {
    using Fn = float(__thiscall*)(World*, float, float);
    World* world = TheWorld();
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(world))[0x70 / 4])(world, x, y);
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

__declspec(naked) ScriptObjectRef* CScript::NewTempObject() EI_ORIGINAL(0x004B59F0)
__declspec(naked) MfcArray<ScriptAreaPart>* CScript::FindArea(int) EI_ORIGINAL(0x004BCE10)

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
EI_REBIRTH_IMPLEMENTS(0x004B4030, ei::rebirth::Patch::AddNewScriptCommands);

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
        static const std::size_t kStatOffset[] = {0x28, 0x2C, 0x34, 0x38};
        CEngineObject* o = objectArgs.data[0]->object;
        void* stats = o ? Field<void*>(o, 0x170) : nullptr;
        if (stats) CopyFloat(result, &Field<float>(stats, kStatOffset[id - 0x4F]));
        else StoreBits(result, kZero);
        return;
    }
    case 0x55:   // GetX
    case 0x56: { // GetY
        CEngineObject* o = objectArgs.data[0]->object;
        if (o) CopyFloat(result, &Field<float>(o, id == 0x55 ? 0x1C : 0x20));
        else StoreBits(result, kZero);
        return;
    }
    case 0x57: { // GetZ: height above the ground at the object's position
        CEngineObject* o = objectArgs.data[0]->object;
        if (!o) {
            StoreBits(result, kZero);
            return;
        }
        float x = Field<float>(o, 0x1C), y = Field<float>(o, 0x20), z = Field<float>(o, 0x24);
        if (o->GetClassId() == kClassUnit) *result = reinterpret_cast<UnitGround*>(o)->Height(x, y) + z;
        else *result = TheWorld()->TerrainHeight(x, y) + z;
        return;
    }
    case 0xE1:   // GetLootItemsCount
    case 0xE2: { // GetMoney
        StoreBits(result, kZero);
        void* player = TheWorld()->GetPlayer(Ftol(f[0]));
        if (player) *result = static_cast<float>(Field<int>(player, id == 0xE1 ? 0xEC : 0xC0));
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
        float buffer[3];
        const float* position = FuturePosition(o, buffer, RoundTimeOffset(GameTime(), f[0]));
        std::memcpy(result, &position[id == 0xBF ? 0 : 1], 4);
        return;
    }
    case 0x99: { // IsUnitInWater
        CEngineObject* o = objectArgs.data[0]->object;
        int inWater = 0;
        if (o) inWater = reinterpret_cast<WaterMap*>(0x007B00D0)->IsInWater(&Field<float>(o, 0x1C)) ? 1 : 0;
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
        std::uint32_t value = reinterpret_cast<IsaacRandom*>(0x007A9D40)->Next();
        *result = ScaleRandom(value, range);
        return;
    }
    case 0x60: { // GetDiplomacy: 1 allied, -1 enemies, 0 otherwise
        int b = Ftol(f[1]);
        int a = Ftol(f[0]);
        int relation = reinterpret_cast<Diplomacy*>(0x007C2BC4)->Get(a, b);
        StoreBits(result, kZero);
        if (relation == 2) StoreBits(result, kMinusOne);
        else if (relation == 0) StoreBits(result, kOne);
        return;
    }
    case 0x63:   // IsAlive
    case 0x64: { // IsDead
        CEngineObject* o = objectArgs.data[0]->object;
        void* stats = o ? Field<void*>(o, 0x170) : nullptr;
        StoreBits(result, kZero);
        if (!stats) return;
        int state = DeathState(stats);
        *result = static_cast<float>(id == 0x63 ? (state == 0 ? 1 : 0) : state);
        return;
    }
    case 0x61: { // IsEnemy(unit, player)
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kZero);
        if (!unit) return;
        int player = Ftol(f[0]);
        std::uint32_t mask = 1u << (player & 31);
        bool enemy = (mask & Field<std::uint32_t>(unit, 0x248) & Field<std::uint32_t>(unit, 0x244)) != 0;
        *result = static_cast<float>(enemy ? 1 : 0);
        return;
    }
    case 0x65: { // GetPlayer
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kZero);
        if (unit) *result = static_cast<float>(Field<int>(unit, 0x250));
        return;
    }
    case 0x8F: { // GetAIClass
        CEngineObject* unit = AsUnit(objectArgs.data[0]->object);
        StoreBits(result, kMinusOne);
        if (unit) *result = static_cast<float>(Field<int>(unit, 0x324));
        return;
    }
    case 0x90: { // GetLeverState
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (!o || o->GetClassId() != kClassLever) return;
        *result = static_cast<float>(Field<int>(o, 0x238));
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
        if (a && b) *result = Distance(Field<float>(a, 0x1C), Field<float>(a, 0x20), Field<float>(b, 0x1C), Field<float>(b, 0x20));
        else StoreBits(result, kFar);
        return;
    }
    case 0x54: { // DistanceUnitPoint
        CEngineObject* o = objectArgs.data[0]->object;
        if (o) *result = Distance(Field<float>(o, 0x1C), Field<float>(o, 0x20), f[0], f[1]);
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
        auto* player = static_cast<Player*>(TheWorld()->GetPlayer(Ftol(f[0])));
        if (player && player->HasItem(Ftol(f[1]))) StoreBits(result, kOne);
        return;
    }
    case 0xB1:   // IsPlayerInDanger
    case 0xB2: { // IsPlayerInSafety
        StoreBits(result, kZero);
        void* player = TheWorld()->GetPlayer(Ftol(f[0]));
        if (!player) return;
        int danger = Field<int>(Field<void*>(player, 0x20), 0x5120);
        if (id == 0xB1 ? danger == 2 : danger == 0) StoreBits(result, kOne);
        return;
    }
    case 0xD1:   // GetMercsNumber
        *result = static_cast<float>(TheWorld()->MercsNumber(Ftol(f[0])));
        return;
    case 0x80: { // GSGetVar(player, name): from the game server in multiplayer, else the player's
        GameServer* server = reinterpret_cast<Network*>(0x007AFE20)->IsMultiplayer() ? ActiveServer() : nullptr;
        if (server) {
            const char* name = stringArgs.data[0];
            stlp::string key(name);
            *result = server->GetVar(&key);
            return;
        }
        StoreBits(result, kZero);
        void* player = TheWorld()->GetPlayer(Ftol(f[0]));
        if (player) *result = reinterpret_cast<QuestVars*>(static_cast<char*>(player) + 0xC4)->Get(stringArgs.data[0]);
        return;
    }
    case 0xA2:   // IsCameraPlaying
        *result = static_cast<float>(reinterpret_cast<CameraPlayer*>(0x007AFEA5)->IsPlaying());
        return;
    case 0xAA:   // IsNight
        CopyFloat(result, reinterpret_cast<const float*>(0x007AFFE4));
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
        float x = Field<float>(o, 0x1C), y = Field<float>(o, 0x20);
        bool inside = !(Compare(x, f[0]) & kLess) && (Compare(x, f[2]) & kLessOrEqual) &&
                      !(Compare(y, f[1]) & kLess) && (Compare(y, f[3]) & kLessOrEqual);
        *result = static_cast<float>(inside ? 1 : 0);
        return;
    }
    case 0xE5: { // IsUnitVisible: in the local player's list of visible units
        CEngineObject* o = objectArgs.data[0]->object;
        StoreBits(result, kZero);
        if (!o) return;
        void* player = TheWorld()->GetPlayer(0);
        if (!player) return;
        auto& visible = Field<MfcArray<CEngineObject*>>(Field<void*>(player, 0x20), 0x510C);
        bool found = false;
        for (int i = 0; i < visible.size; ++i)
            if (visible.data[i] == o) {
                found = true;
                break;
            }
        *result = static_cast<float>(found ? 1 : 0);
        return;
    }
    case 0xBD:   // GetZValue
        *result = TheWorld()->TerrainHeight(f[0], f[1]);
        return;
    case 0xBE:   // GetBSZValue
        *result = WorldBszValue(f[0], f[1]);
        return;
    case 0xC5: { // IsInArea(area, x, y)
        int area = x87::fistp(f[0]);
        float x = f[1], y = f[2];
        const MfcArray<ScriptAreaPart>* parts = script->FindArea(area);
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
EI_REBIRTH_IMPLEMENTS(0x004B41E0, ei::rebirth::Patch::AddNewScriptCommands);

void SSCCmd::CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result) {
    PrepareArguments(script, vars);
    *result = script->NewTempObject();
    if (scriptaddon::Enabled()) {   // AddNewScriptCommands (hook at 004B572F)
        scriptaddon::Call call{this, script, result, nullptr, nullptr};
        if (scriptaddon::Dispatch(&call)) return;
    }
    switch (id) {
    case 0x4E: { // GetObject(id)
        CEngineObject* o = TheWorld()->FindObject(Ftol(floatArgs.data[0]));
        if (!o) game::DebugLog("Object with ID %d not found in function \"%s\".\n", Ftol(floatArgs.data[0]), "GetObject");
        ScriptObjectRef* slot = *result;
        slot->Reset();
        if (o) ++o->refCount_;
        slot->object = o;
        return;
    }
    case 0xB4: { // GetUnitOfPlayer(player, index)
        void* player = TheWorld()->GetPlayer(Ftol(floatArgs.data[0]));
        CEngineObject* o = nullptr;
        if (player) {
            int index = x87::fistp(floatArgs.data[1]);
            if (index >= 0 && index < Field<int>(player, 0xA4))
                o = TheWorld()->FindUnitObject(Field<int>(Field<char*>(player, 0xA0) + index * 0x784, 0x1C));
        }
        Store(*result, o);
        return;
    }
    case 0x85: { // GetObjectByName
        CEngineObject* o = TheWorld()->FindObject(ObjectIdFromName(stringArgs.data[0]));
        if (!o)
            game::DebugLog("Object with name %s not found in function \"%s\".\n",
                           static_cast<const char*>(stringArgs.data[0]), "GetObjectByName");
        Store(*result, o);
        return;
    }
    case 0xAB: { // GetObjectByID (decimal string)
        CEngineObject* o = TheWorld()->FindObject(GameAtoi(stringArgs.data[0]));
        if (!o)
            game::DebugLog("Object with ID %s not found in function \"%s\".\n",
                           static_cast<const char*>(stringArgs.data[0]), "GetObjectByID");
        Store(*result, o);
        return;
    }
    case 0xE4: { // GetLeader: the server's leader in multiplayer, else player 0's first unit
        int unitId;
        if (GameServer* server = ActiveServer()) {
            unitId = server->GetLeaderId();
        } else {
            unitId = 0x3B9AF5CE;
            void* player = TheWorld()->GetPlayer(0);
            if (player && Field<int>(player, 0xA4) > 0) unitId = Field<int>(Field<char*>(player, 0xA0), 0x1C);
        }
        Store(*result, TheWorld()->FindObject(unitId));
        return;
    }
    default:
        game::FatalError("Function does not return object at SSCCmd::CalcFunction");
        return;
    }
}
EI_REPLACE_METHOD(0x004B56C0, SSCCmd::CalcObject);
EI_REBIRTH_IMPLEMENTS(0x004B56C0, ei::rebirth::Patch::AddNewScriptCommands);

// ---- CalcGroup ----------------------------------------------------------------------------------

namespace {

// The script's temporary groups (CList<ScriptGroup> at +0x1B0): AddTail (004C8D80) copies the group
// into a new node (the group at node + 8) and returns the node. 004C2290: group destructor.
// 004C1290: group assignment.
__declspec(naked) char* __fastcall GroupTempsAddTail(void* /*list*/, void*, const ScriptGroup*) EI_ORIGINAL(0x004C8D80)
__declspec(naked) void __fastcall GroupDestruct(ScriptGroup*) EI_ORIGINAL(0x004C2290)
__declspec(naked) void __fastcall GroupAssign(ScriptGroup*, void*, const ScriptGroup*) EI_ORIGINAL(0x004C1290)

// Walks the world object manager's list (007AFFA0); holds a reference on the manager meanwhile.
struct ObjectIterator {
    void* First(void* list);   // 00404920
    void* Next();              // 00404960
    void* list;
    void* node;
};
__declspec(naked) void* ObjectIterator::First(void*) EI_ORIGINAL(0x00404920)
__declspec(naked) void* ObjectIterator::Next() EI_ORIGINAL(0x00404960)

// FNSTSW after FILD a, FCOMP b (AH).
int CompareIntFloat(int a, float b) {
    int status;
    __asm {
        fild a
        fcomp b
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return status;
}

// What a unit sees: its perception object (+0x240) lists the objects at +0xA94 ({next, ?, object}).
void InsertSeen(ScriptGroup* group, char* perception) {
    for (char* node = Field<char*>(perception, 0xA94); node;) {
        char* current = node;
        node = Field<char*>(current, 0);
        group->Insert(Field<CEngineObject*>(current, 8));
    }
}

// A player's party: an STLport hash_map<id, unit> at player + 0x40 (as in combat_actions.cpp).
struct PartyNode {
    PartyNode* next;
    std::uint32_t key;
    CEngineObject* unit;
};
struct PartyMap {
    std::uint32_t functors;
    PartyNode** buckets;
    PartyNode** finish;
};

std::uint32_t BucketCount(const PartyMap* map) {
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(map->finish) -
                                                       reinterpret_cast<std::uintptr_t>(map->buckets)) >> 2);
}

// 00455830 (hashtable::begin): the head of the first non-empty bucket.
PartyNode* FirstNode(const PartyMap* map) {
    for (std::uint32_t i = 0; i < BucketCount(map); ++i)
        if (map->buckets[i]) return map->buckets[i];
    return nullptr;
}

// operator++ (004558B0 when the chain ends).
PartyNode* NextNode(const PartyMap* map, const PartyNode* node) {
    if (node->next) return node->next;
    std::uint32_t bucket = node->key % BucketCount(map);
    for (;;) {
        ++bucket;
        if (BucketCount(map) <= bucket) return nullptr;
        if (PartyNode* head = map->buckets[bucket]) return head;
    }
}

} // namespace

void SSCCmd::CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result) {
    ObjectIterator objects;
    objects.list = nullptr;
    PrepareArguments(script, vars);
    {
        ScriptGroup empty;   // 004B5ED0
        empty.vtable = reinterpret_cast<void*>(0x0073D110);
        empty.data = nullptr;
        empty.size = 0;
        empty.maxSize = 0;
        empty.growBy = 0;
        char* node = GroupTempsAddTail(script->Field(0x1B0), nullptr, &empty);
        GroupDestruct(&empty);
        *result = reinterpret_cast<ScriptGroup*>(node + 8);
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
        if (char* perception = Field<char*>(unit, 0x240)) InsertSeen(*result, perception);
        break;
    }
    case 0x5E: { // GroupSee(group): what its units see
        group->SetSize(0, -1);
        for (int i = 0; i < groupArgs.data[0]->size; ++i) {
            CEngineObject* unit = groupArgs.data[0]->data[i].AsUnit();
            if (!unit) continue;
            if (char* perception = Field<char*>(unit, 0x240)) InsertSeen(*result, perception);
        }
        break;
    }
    case 0x62: { // GetPlayerUnits(player): the world's units whose player compares equal (or NaN)
        for (auto* o = static_cast<CEngineObject*>(objects.First(*reinterpret_cast<void**>(0x007AFFA0))); o;
             o = static_cast<CEngineObject*>(objects.Next())) {
            if (o->GetClassId() != kClassUnit) continue;
            if (CompareIntFloat(Field<int>(o, 0x250), floatArgs.data[0]) & kEqual) (*result)->Insert(o);
        }
        break;
    }
    case 0xA5: { // PlayerSee(player): the player's seen objects, its party and what the party sees
        void* player = TheWorld()->GetPlayer(Ftol(floatArgs.data[0]));
        if (!player) {
            (*result)->SetSize(0, -1);
            break;
        }
        GroupAssign(*result, nullptr, reinterpret_cast<const ScriptGroup*>(Field<char*>(player, 0x20) + 0x50F8));
        const auto* party = reinterpret_cast<const PartyMap*>(static_cast<char*>(player) + 0x40);
        for (PartyNode* node = FirstNode(party); node; node = NextNode(party, node)) {
            CEngineObject* unit = node->unit;
            if (!unit) break;
            (*result)->Insert(unit);
            InsertSeen(*result, Field<char*>(unit, 0x240));   // no null check, as in the original
        }
        break;
    }
    default:
        game::FatalError(reinterpret_cast<const char*>(0x0078B748));   // "Function does not return string at ..."
        break;
    }
    if (objects.list) --Field<int>(objects.list, 4);   // the iterator's destructor
}
EI_REPLACE_METHOD(0x004B5B00, SSCCmd::CalcGroup);
EI_REBIRTH_IMPLEMENTS(0x004B5B00, ei::rebirth::Patch::AddNewScriptCommands);
