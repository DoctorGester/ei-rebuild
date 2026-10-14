// CWorldServer::Update (vtable slot 6, 0058DFF0): one world update. See world.h and
// docs/notes/world.md.
#include "world.h"

#include "combat.h"
#include "core_maths.h"
#include "game_flow_data.h"
#include "net_classes.h"
#include "net_link.h"
#include "player_messages.h"
#include "script.h"
#include "unit.h"
#include "world_data.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"

// The radius keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

void* NetworkLog() { return reinterpret_cast<void*>(flowdata::kNetLogStream); }

// The player state's +0x24 (CPlayerServer::possessedId_): negative for the local players.
bool IsLocal(const CPlayerServer* state) { return static_cast<int>(state->possessedId_) < 0; }

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

__declspec(naked) int __fastcall SyncPlayerAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x0058B750)
// 0058DAF0 (RET 4). Accepts the players a pending connection object holds.
__declspec(naked) void __fastcall AcceptPlayers(CWorldServer*, void*, void* /*source*/) EI_ORIGINAL(0x0058DAF0)
// 00479E30 (RET 4). The pending object at +0x60 polls the game server.
// 00665A10. Disconnects a player (its connection and units).
void __fastcall PlayerDisconnect(WorldPlayer* a0, void*) { PlayerDisconnectOriginal((void*)a0); }
// 0066C1D0 (RET 4). The player list releases the player.
// 0066E130. A player's state's update after the tick.
__declspec(naked) void __fastcall StateUpdate(void*, void*) EI_ORIGINAL(0x0066E130)
// Script groups: constructor 004C2030 (base vtable), destructor 004C2290, assignment 004C1290,
// union 004B2620; the global group / name / element getters by value (004C3B00, 004C2680,
// 004C21E0); 004CC7D0 (__stdcall) releases an element range; 004CE1E0 (RET 4) one element's
// destructor; 004C2050 SetSize.
void __fastcall GroupConstruct(ScriptGroup* a0, void*) { ScriptGroupConstructOriginal((void*)a0); }
inline void __fastcall GroupDestruct(ScriptGroup* a0, void*) { ScriptGroupDestructOriginal((void*)a0); }
inline void __fastcall GroupAssign(ScriptGroup* a0, void*, const ScriptGroup* a2) { GroupAssignOriginal((void*)a0, nullptr, (const void*)a2); }
__declspec(naked) void __fastcall GroupUnion(ScriptGroup*, void*, const ScriptGroup*, const ScriptGroup*) EI_ORIGINAL(0x004B2620)
ScriptGroup* __fastcall GroupsGetAt(void* a0, void*, ScriptGroup* a2, int a3) { return (ScriptGroup*)ScriptGroupAtOriginal((const void*)a0, nullptr, (void*)a2, a3); }
mfc::CString* __fastcall NamesGetAt(void* a0, void*, mfc::CString* a2, int a3) { return (mfc::CString*)ScriptStringAtOriginal((const void*)a0, nullptr, (void*)a2, a3); }
ScriptObjectRef* __fastcall GroupGetAt(const ScriptGroup* a0, void*, ScriptObjectRef* a2, int a3) { return (ScriptObjectRef*)ScriptRefAtOriginal((const void*)a0, nullptr, (void*)a2, a3); }
void __stdcall RefsRelease(ScriptObjectRef* a0, int a1) { ScriptRefElementsDestruct((void*)a0, a1); }
void __fastcall GroupSetSize(ScriptGroup* a0, void*, int a2, int a3) { PtrArraySetSize((void*)a0, nullptr, a2, a3); }
// The network log's ostream (007C56B0).
inline void* __fastcall OstreamPutLong(void* s, void*, long v) { return OstreamPutLongOriginal(s, nullptr, v); }

// A player that lost its connection leaves the game.
void DropPlayer(CWorldServer* world, WorldPlayer* player) {
    PlayerDisconnect(player, nullptr);
    PlayerListRelease(world->PlayerList(), nullptr, player);
    void* log = OstreamPutString(NetworkLog(), nullptr, worlddata::DisconnectingPlayerText());
    log = OstreamPutLong(log, nullptr, player->id);
    OstreamManipulate(OstreamPutChar(log, nullptr, '\n'), nullptr, worlddata::FlushManipulator());
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer
// computes it, with another margin: the server half's movement factor 3 and statistics B0
// values 0 and 2).
void WakeAroundUnits(CWorldServer* world, WorldPlayer* player) {
    auto& units = player->units.map;
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        auto* unit = static_cast<CUnit*>(node->value);
        auto* controller = static_cast<CUnitServer*>(unit->controller);
        float perception = StoreFloat(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = !(perception >= minimum) ? minimum : perception;   // the branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * worlddata::HearingScale() + worlddata::WakeMargin());
        WorldWakeUnits(world, nullptr, &unit->position, radius);
    }
}

void InitGroup(ScriptGroup* group) {
    GroupConstruct(group, nullptr);
    group->vtable = scriptvtable::kRefs;
}

} // namespace

int CWorldServer::Update() {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        netlink::NetLink* link = player->link;
        if (link && link->Connected() && player->Connection()->Receive() != 0)
            WakeAroundUnits(this, player);
        else
            DropPlayer(this, player);
    }

    if (CRefObject* handle = shared5C_.ptr) {
        static_cast<CWorldHandle*>(handle)->Pump();
        AcceptPlayers(this, nullptr, shared5C_.ptr);
    }
    if (void* pending = netSession_) {
        net::ServerSession* server = net::GameServer();
        if (server && !(server->flags7_ & CRefCounted::kDetached)) SessionPump(pending, nullptr, server);
        AcceptPlayers(this, nullptr, netSession_);
    }

    if (!paused_) {
        for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
            PlayerTickOriginal(player);
        TickAt(this, nullptr, 0);
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        StateUpdate(player->server, nullptr);

    // The local players' heroes: the global "Heroes" group plus each one's own heroes, then given
    // back to every local player.
    ScriptGroup heroes;
    InitGroup(&heroes);
    ScriptVars& globals = MainScript()->globals_;
    auto& names = globals.groupNames;
    for (int i = 0; i < names.size; ++i) {
        bool found;
        {
            alignas(4) unsigned char storage[sizeof(mfc::CString)];
            mfc::CString* name = NamesGetAt(&names, nullptr, reinterpret_cast<mfc::CString*>(storage), i);
            found = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*name)),
                                    reinterpret_cast<const unsigned char*>(worlddata::HeroesGroupText())) == 0;
            name->~CString();
        }
        if (found) {
            alignas(4) std::uint8_t storage[sizeof(ScriptGroup)];
            ScriptGroup* group = GroupsGetAt(&globals.groups, nullptr, reinterpret_cast<ScriptGroup*>(storage), i);
            GroupAssign(&heroes, nullptr, group);
            GroupDestruct(reinterpret_cast<ScriptGroup*>(storage), nullptr);
            break;
        }
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->server;
        if (!IsLocal(state)) continue;
        ScriptGroup sum;
        InitGroup(&sum);
        GroupUnion(&sum, nullptr, &state->partyUnits_, &heroes);
        GroupAssign(&heroes, nullptr, &sum);
        sum.vtable = scriptvtable::kRefsBase;
        if (sum.data) {
            RefsRelease(sum.data, sum.size);
            gamecrt::operator_delete(sum.data);
        }
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->server;
        if (!IsLocal(state)) continue;
        ScriptGroup& own = state->partyUnits_;
        GroupSetSize(&own, nullptr, heroes.size, -1);
        for (int i = 0; i < heroes.size; ++i) {
            ScriptObjectRef copy;
            ScriptObjectRef* ref = GroupGetAt(&heroes, nullptr, &copy, i);
            ScriptObjectRef* slot = &state->partyUnits_.data[i];
            RefReset(slot);
            RefConstruct(slot, nullptr, ref);
            RefReset(&copy);
        }
    }
    heroes.vtable = scriptvtable::kRefsBase;
    if (heroes.data) {
        ScriptObjectRef* ref = heroes.data;
        for (int n = heroes.size; n != 0; --n) ScriptRefDestroyOriginal(ref++, nullptr, 0);
        gamecrt::operator_delete(heroes.data);
    }

    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayerAt(this, nullptr, player)) DropPlayer(this, player);

    ++updateCount_;
    return 1;
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0058DFF0, CWorldServer::Update);
