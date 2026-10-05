// CWorldServer::Update (vtable slot 6, 0058DFF0): one world update. See world.h and
// docs/notes/world.md ("World tick" and seventh round).
#include "world.h"

#include "core_maths.h"
#include "script.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_script.h"
#include "originals_world.h"

// The radius keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

constexpr std::uintptr_t kDisconnecting = 0x00790894;   // "Disconnecting player "
constexpr std::uintptr_t kHeroes = 0x007908C0;          // "Heroes"
constexpr std::uintptr_t kGroupVtable = 0x0073D110;     // ScriptGroup
constexpr std::uintptr_t kGroupBaseVtable = 0x0073D11C; // its CArray base (destructor)
void* const kNetworkLog = reinterpret_cast<void*>(0x007C56B0);
void* const kFlush = reinterpret_cast<void*>(0x00435000);

using UnitMap = stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>;

// WorldPlayer fields.
constexpr int kPlayerConnection = 0x1C;   // slot 4 (+0x10): still connected (AL)
constexpr int kPlayerState = 0x20;        // CPlayerServer: slot 23 (+0x5C) non-zero while it plays
constexpr int kPlayerUnits = 0x40;
// The player state's +0x24 (negative for the local players) and its group of heroes (+0x510C).
constexpr int kStateTeam = 0x24;
constexpr int kStateHeroes = 0x510C;

// The global script variables (CWorldServer + 0x12914, a ScriptVars): the groups and their names.
constexpr int kGlobalGroups = 0x1298C;
constexpr int kGlobalGroupNames = 0x129A0;

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

__declspec(naked) void __fastcall WakeUnitsAroundAt(CWorldServer*, void*, const void* /*position*/, float /*radius*/)
    EI_ORIGINAL(0x0058D3D0)
__declspec(naked) int __fastcall SyncPlayerAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x0058B750)
// 0058DAF0 (RET 4). Accepts the players a pending connection object holds.
__declspec(naked) void __fastcall AcceptPlayers(CWorldServer*, void*, void* /*source*/) EI_ORIGINAL(0x0058DAF0)
// 00479E30 (RET 4). The pending object at +0x60 polls the game server.
// 00665A10. Disconnects a player (its connection and units).
__declspec(naked) void __fastcall PlayerDisconnect(WorldPlayer*, void*) EI_ORIGINAL(0x00665A10)
// 0066C1D0 (RET 4). The player list releases the player.
// 00665DE0 / 0066E130. A player's update before the tick / its state's update after it.
__declspec(naked) void __fastcall PlayerUpdate(WorldPlayer*, void*) EI_ORIGINAL(0x00665DE0)
__declspec(naked) void __fastcall StateUpdate(void*, void*) EI_ORIGINAL(0x0066E130)
// Script groups: constructor 004C2030 (base vtable), destructor 004C2290, assignment 004C1290,
// union 004B2620; the global group / name / element getters by value (004C3B00, 004C2680,
// 004C21E0); 004CC7D0 (__stdcall) releases an element range; 004CE1E0 (RET 4) one element's
// destructor; 004C2050 SetSize.
__declspec(naked) void __fastcall GroupConstruct(ScriptGroup*, void*) EI_ORIGINAL(0x004C2030)
__declspec(naked) void __fastcall GroupDestruct(ScriptGroup*, void*) EI_ORIGINAL(0x004C2290)
__declspec(naked) void __fastcall GroupAssign(ScriptGroup*, void*, const ScriptGroup*) EI_ORIGINAL(0x004C1290)
__declspec(naked) void __fastcall GroupUnion(ScriptGroup*, void*, const ScriptGroup*, const ScriptGroup*) EI_ORIGINAL(0x004B2620)
__declspec(naked) ScriptGroup* __fastcall GroupsGetAt(void*, void*, ScriptGroup* /*out*/, int) EI_ORIGINAL(0x004C3B00)
__declspec(naked) mfc::CString* __fastcall NamesGetAt(void*, void*, mfc::CString* /*out*/, int) EI_ORIGINAL(0x004C2680)
__declspec(naked) ScriptObjectRef* __fastcall GroupGetAt(const ScriptGroup*, void*, ScriptObjectRef* /*out*/, int)
    EI_ORIGINAL(0x004C21E0)
__declspec(naked) void __stdcall RefsRelease(ScriptObjectRef*, int) EI_ORIGINAL(0x004CC7D0)
__declspec(naked) void* __fastcall RefDestruct(ScriptObjectRef*, void*, unsigned /*flags*/) EI_ORIGINAL(0x004CE1E0)
__declspec(naked) void __fastcall GroupSetSize(ScriptGroup*, void*, int, int) EI_ORIGINAL(0x004C2050)
__declspec(naked) void __fastcall RefReset(ScriptObjectRef*, void*) EI_ORIGINAL(0x004B24B0)
// The network log's ostream (007C56B0).
__declspec(naked) void* __fastcall OstreamPutString(void*, void*, const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) void* __fastcall OstreamPutLong(void*, void*, long) EI_ORIGINAL(0x006FC9C6)
__declspec(naked) void* __fastcall OstreamPutChar(void*, void*, char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) void* __fastcall OstreamManipulate(void*, void*, void*) EI_ORIGINAL(0x00434FE0)

constexpr int kSlotPlayers = 20;
constexpr int kSlotConnected = 4;
constexpr int kSlotPlaying = 23;
constexpr int kSlotPendingStep = 5;

// A player that lost its connection leaves the game.
void DropPlayer(CWorldServer* world, WorldPlayer* player) {
    PlayerDisconnect(player, nullptr);
    PlayerListRelease(ei::VCall<CEngineObject*>(world, kSlotPlayers), nullptr, player);
    void* log = OstreamPutString(kNetworkLog, nullptr, reinterpret_cast<const char*>(kDisconnecting));
    log = OstreamPutLong(log, nullptr, player->id);
    OstreamManipulate(OstreamPutChar(log, nullptr, '\n'), nullptr, kFlush);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer
// computes it, with another margin).
void WakeAroundUnits(CWorldServer* world, WorldPlayer* player) {
    auto& units = At<UnitMap>(player, kPlayerUnits);
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CEngineObject* unit = node->value;
        auto* controller = At<std::uint8_t*>(unit, 0x240);
        float perception = StoreFloat(At<float>(controller, 0x904) * At<float>(controller, 0x294));
        float minimum = At<float>(controller, 0x2A4);
        float range = !(perception >= minimum) ? minimum : perception;   // the branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * GameFloat(0x0073F910) + GameFloat(0x0073CCF8));
        WakeUnitsAroundAt(world, nullptr, &At<std::uint8_t>(unit, 0x1C), radius);
    }
}

void InitGroup(ScriptGroup* group) {
    GroupConstruct(group, nullptr);
    group->vtable = reinterpret_cast<void*>(kGroupVtable);
}

} // namespace

int CWorldServer::Update() {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next()) {
        void* connection = At<void*>(player, kPlayerConnection);
        if (connection && ei::VCall<bool>(connection, kSlotConnected) &&
            ei::VCall<int>(At<void*>(player, kPlayerState), kSlotPlaying) != 0)
            WakeAroundUnits(this, player);
        else
            DropPlayer(this, player);
    }

    if (void* pending = At<void*>(this, 0x5C)) {
        ei::VCall<void>(pending, kSlotPendingStep);
        AcceptPlayers(this, nullptr, At<void*>(this, 0x5C));
    }
    if (void* pending = unknown60_) {
        std::uint8_t* server = *reinterpret_cast<std::uint8_t**>(0x0079B5DC);
        if (server && !(At<std::uint32_t>(server, 4) & 0x80000000u)) SessionPump(pending, nullptr, server);
        AcceptPlayers(this, nullptr, unknown60_);
    }

    if (!unknown6C_) {
        for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next())
            PlayerUpdate(player, nullptr);
        TickAt(this, nullptr, 0);
    }
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next())
        StateUpdate(At<void*>(player, kPlayerState), nullptr);

    // The local players' heroes: the global "Heroes" group plus each one's own heroes, then given
    // back to every local player.
    ScriptGroup heroes;
    InitGroup(&heroes);
    auto& names = At<MfcArray<mfc::CString>>(this, kGlobalGroupNames);
    for (int i = 0; i < names.size; ++i) {
        bool found;
        {
            alignas(4) unsigned char storage[sizeof(mfc::CString)];
            mfc::CString* name = NamesGetAt(&names, nullptr, reinterpret_cast<mfc::CString*>(storage), i);
            found = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*name)),
                                    reinterpret_cast<const unsigned char*>(kHeroes)) == 0;
            name->~CString();
        }
        if (found) {
            alignas(4) std::uint8_t storage[sizeof(ScriptGroup)];
            ScriptGroup* group = GroupsGetAt(&At<std::uint8_t>(this, kGlobalGroups), nullptr,
                                             reinterpret_cast<ScriptGroup*>(storage), i);
            GroupAssign(&heroes, nullptr, group);
            GroupDestruct(reinterpret_cast<ScriptGroup*>(storage), nullptr);
            break;
        }
    }
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next()) {
        void* state = At<void*>(player, kPlayerState);
        if (At<int>(state, kStateTeam) >= 0) continue;
        ScriptGroup sum;
        InitGroup(&sum);
        GroupUnion(&sum, nullptr, &At<ScriptGroup>(state, kStateHeroes), &heroes);
        GroupAssign(&heroes, nullptr, &sum);
        sum.vtable = reinterpret_cast<void*>(kGroupBaseVtable);
        if (sum.data) {
            RefsRelease(sum.data, sum.size);
            gamecrt::operator_delete(sum.data);
        }
    }
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next()) {
        void* state = At<void*>(player, kPlayerState);
        if (At<int>(state, kStateTeam) >= 0) continue;
        auto& own = At<ScriptGroup>(state, kStateHeroes);
        GroupSetSize(&own, nullptr, heroes.size, -1);
        for (int i = 0; i < heroes.size; ++i) {
            ScriptObjectRef copy;
            ScriptObjectRef* ref = GroupGetAt(&heroes, nullptr, &copy, i);
            ScriptObjectRef* slot = &At<ScriptGroup>(state, kStateHeroes).data[i];
            RefReset(slot, nullptr);
            RefConstruct(slot, nullptr, ref);
            RefReset(&copy, nullptr);
        }
    }
    heroes.vtable = reinterpret_cast<void*>(kGroupBaseVtable);
    if (heroes.data) {
        ScriptObjectRef* ref = heroes.data;
        for (int n = heroes.size; n != 0; --n) RefDestruct(ref++, nullptr, 0);
        gamecrt::operator_delete(heroes.data);
    }

    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, kSlotPlayers)); player; player = it.Next())
        if (!SyncPlayerAt(this, nullptr, player)) DropPlayer(this, player);

    ++updateCount_;
    return 1;
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0058DFF0, CWorldServer::Update);
