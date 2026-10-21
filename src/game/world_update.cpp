// CWorldServer::Update (vtable slot 6, 0058DFF0): one world update. See world.h and
// docs/notes/world.md.
#include "world.h"
#include "lib/crt_iostream.h"

#include "combat.h"
#include "game_arrays.h"
#include "core_maths.h"
#include "game_flow_data.h"
#include "net_classes.h"
#include "net_link.h"
#include "player_manager.h"
#include "player_messages.h"
#include "script.h"
#include "unit.h"
#include "world_data.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "lib/mfc_array.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"
#include "game_data.h"

namespace {

libcrt::Ostream* NetworkLog() { return ei::GamePointer<libcrt::Ostream>(flowdata::kNetLogStream); }

// CPlayerServer::possessedId_ (+0x24) is -1 while no Possession spell has taken a unit.
bool NoPendingPossession(const CPlayerServer* state) { return static_cast<int>(state->possessedId_) < 0; }

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

__declspec(naked) int __fastcall SyncPlayerAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x0058B750)
// 00665A10. Disconnects a player (its connection and units).
void __fastcall PlayerDisconnect(WorldPlayer* a0, void*) { PlayerDisconnectOriginal((void*)a0); }
// Script groups: constructor 004C2030 (base vtable), destructor 004C2290, assignment 004C1290,
// union 004B2620; the global group / name / element getters by value (004C3B00, 004C2680,
// 004C21E0); 004CC7D0 (__stdcall) releases an element range; 004CE1E0 (RET 4) one element's
// destructor; 004C2050 SetSize.
inline void __fastcall GroupDestruct(ScriptGroup* a0, void*) { ScriptGroupDestructOriginal((void*)a0); }
ScriptObjectRef* __fastcall GroupGetAt(const ScriptGroup* a0, void*, ScriptObjectRef* a2, int a3) {
    return gamearrays::ObjectRefGetAt(reinterpret_cast<const gamearrays::ObjectRefs::Array*>(a0), nullptr, a2, a3);
}
void __stdcall RefsRelease(ScriptObjectRef* a0, int a1) { gamearrays::ObjectRefs::DestructElements(static_cast<ScriptObjectRef*>(a0), a1); }

// A player that lost its connection leaves the game.
void DropPlayer(CWorldServer* world, WorldPlayer* player) {
    PlayerDisconnect(player, nullptr);
    static_cast<CPlayerManager*>(world->PlayerList())->ReleasePlayer(reinterpret_cast<CEngineObject*>(player));
    NetworkLog()->PutText(worlddata::DisconnectingPlayerText())->PutInt(player->id)->PutChar('\n')
        ->Apply(libcrt::FlushManipulator);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer
// computes it, with another margin: the server half's movement factor 3 and statistics B0
// values 0 and 2).
void WakeAroundUnits(CWorldServer* world, WorldPlayer* player) {
    auto& units = player->units.map;
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        auto* unit = static_cast<CUnit*>(node->value);
        auto* controller = static_cast<CUnitServer*>(unit->controller);
        float perception = static_cast<float>(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = perception >= minimum ? perception : minimum;   // the branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * worlddata::HearingScale() + worlddata::WakeMargin());
        world->WakeUnitsAround(&unit->position, radius);
    }
}

void InitGroup(ScriptGroup* group) {
    mfc::impl::ConstructHeader<mfc::impl::ObjectRefArrayClass>((void*)group);
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
        AcceptNewPlayers(shared5C_.ptr);
    }
    if (void* pending = netSession_) {
        net::ServerSession* server = net::GameServer();
        if (server && !(server->flags7_ & CRefCounted::kDetached)) static_cast<netlink::DriverSession*>(pending)->PumpDrivers(server);
        AcceptNewPlayers(netSession_);
    }

    if (!paused_) {
        for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
            PlayerTickOriginal(player);
        Tick(static_cast<char>(0));
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->server->UpdatePartyUnits();

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
            mfc::CString* name = reinterpret_cast<mfc::CString*>(mfc::impl::StringArrayAbi::GetAt(
        reinterpret_cast<const mfc::impl::StringArrayAbi::Array*>(&names), nullptr, reinterpret_cast<mfc::impl::CString*>(reinterpret_cast<mfc::CString*>(storage)), i));
            found = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*name)),
                                    reinterpret_cast<const unsigned char*>(worlddata::HeroesGroupText())) == 0;
            name->~CString();
        }
        if (found) {
            alignas(4) std::uint8_t storage[sizeof(ScriptGroup)];
            ScriptGroup* group = gamearrays::GroupGetAt(reinterpret_cast<const gamearrays::Groups::Array*>(&globals.groups), nullptr, reinterpret_cast<ScriptGroup*>(storage), i);
            gamearrays::ObjectRefCopy(reinterpret_cast<gamearrays::ObjectRefs::Array*>(&heroes), nullptr,
                              reinterpret_cast<const gamearrays::ObjectRefs::Array*>(group));
            GroupDestruct(reinterpret_cast<ScriptGroup*>(storage), nullptr);
            break;
        }
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->server;
        if (!NoPendingPossession(state)) continue;
        ScriptGroup sum;
        InitGroup(&sum);
        sum.Union(state->partyUnits_, heroes);
        gamearrays::ObjectRefCopy(reinterpret_cast<gamearrays::ObjectRefs::Array*>(&heroes), nullptr,
                              reinterpret_cast<const gamearrays::ObjectRefs::Array*>(&sum));
        sum.vtable = scriptvtable::kRefsBase;
        if (sum.data) {
            RefsRelease(sum.data, sum.size);
            gamecrt::operator_delete(sum.data);
        }
    }
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->server;
        if (!NoPendingPossession(state)) continue;
        ScriptGroup& own = state->partyUnits_;
        own.SetSize(heroes.size, -1);
        for (int i = 0; i < heroes.size; ++i) {
            ScriptObjectRef copy;
            ScriptObjectRef* ref = GroupGetAt(&heroes, nullptr, &copy, i);
            ScriptObjectRef* slot = &state->partyUnits_.data[i];
            slot->Reset();
            slot->Construct(*ref);
            copy.Reset();
        }
    }
    heroes.vtable = scriptvtable::kRefsBase;
    if (heroes.data) {
        ScriptObjectRef* ref = heroes.data;
        for (int n = heroes.size; n != 0; --n) ScriptRefDestroy(&(ref++)->object, nullptr, 0);
        gamecrt::operator_delete(heroes.data);
    }

    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayerAt(this, nullptr, player)) DropPlayer(this, player);

    ++updateCount_;
    return 1;
}

EI_REPLACE_METHOD(0x0058DFF0, CWorldServer::Update);
