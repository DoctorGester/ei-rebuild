// CWorldServer::Update: one world update. See world.h and docs/notes/world.md.
#include "world.h"
#include "console.h"
#include "lib/static_object.h"
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
#include "game_data.h"

#include <memory>

namespace {

libcrt::Ostream* NetworkLog() { return &console::g_netLogOut; }

// -1 while no Possession spell has taken a unit.
bool NoPendingPossession(const CPlayerServer* state) { return static_cast<int>(state->possessedId_) < 0; }

// ---- Helpers ----------------------------------------------------------------------------------

// Disconnects a player (its connection and units).
// Script groups: constructor (base vtable), destructor, assignment, union; the global group / name / element
// getters by value; a release of an element range (__stdcall), one element's destructor, SetSize.
void __stdcall RefsRelease(ScriptObjectRef* a0, int a1) { gamearrays::ObjectRefs::DestructElements(a0, a1); }

// A player that lost its connection leaves the game.
void DropPlayer(CWorldServer* world, CPlayer* player) {
    player->Disconnect();
    world->PlayerList()->ReleasePlayer(player);
    NetworkLog()->PutText(worlddata::DisconnectingPlayerText())->PutInt(player->playerId_)->PutChar('\n')
        ->Apply(libcrt::FlushManipulator);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer computes it, with
// another margin: the server half's movement factor 3 and statistics B0 values 0 and 2).
void WakeAroundUnits(CWorldServer* world, CPlayer* player) {
    auto& units = player->units_;
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CUnit* unit = node->value;
        auto* controller = unit->ServerController();
        float perception = static_cast<float>(controller->moveFactors[3] * controller->stats.valuesB0[0].current);
        float minimum = controller->stats.valuesB0[2].current;
        float range = perception >= minimum ? perception : minimum;   // the branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * worlddata::HearingScale() + worlddata::WakeMargin());
        world->WakeUnitsAround(&unit->localPosition_, radius);
    }
}

void InitGroup(ScriptGroup* group) {
    mfc::impl::ConstructHeader<mfc::impl::ObjectRefArrayClass>((void*)group);
    group->vtable = mfc::impl::ScriptGroupClass::Vtable();
}

} // namespace

int CWorldServer::Update() {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        netlink::NetLink* link = player->link_;
        if (link && link->Connected() && player->connection_->Receive() != 0)
            WakeAroundUnits(this, player);
        else
            DropPlayer(this, player);
    }

    if (CWorldHandle* handle = shared5C_.Get()) {
        handle->Pump();
        AcceptNewPlayers(handle);
    }
    if (netlink::DriverSession* session = netSession_.Get()) {
        net::ServerSession* server = net::GameServer();
        if (server && !(server->flags7_ & CRefCounted::kDetached)) session->PumpDrivers(server);
        AcceptNewPlayers(session);
    }

    if (!paused_) {
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->Tick();
        Tick(static_cast<char>(0));
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->Server()->UpdatePartyUnits();

    // The local players' heroes: the global "Heroes" group plus each one's own heroes, then given
    // back to every local player.
    ScriptGroup heroes;
    InitGroup(&heroes);
    ScriptVars& globals = MainScript()->globals_;
    auto& names = globals.groupNames;
    for (int i = 0; i < names.size; ++i) {
        bool found;
        {
            lib::InPlace<mfc::CString> storage;
            mfc::CString* name = names.CopyAt(storage.Get(), i);
            found = gamecrt::mbscmp(name->GetString(),
                                    worlddata::HeroesGroupText()) == 0;
            std::destroy_at(name);
        }
        if (found) {
            lib::InPlace<ScriptGroup> storage;
            ScriptGroup* group = globals.groups.CopyAt(storage.Get(), i);
            heroes.CopyFrom(*group);
            storage.Get()->DestructBody();
            break;
        }
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        ScriptGroup sum;
        InitGroup(&sum);
        sum.Union(state->partyUnits_, heroes);
        heroes.CopyFrom(sum);
        sum.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (sum.data) {
            RefsRelease(sum.data, sum.size);
            gamecrt::operator_delete(sum.data);
        }
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        ScriptGroup& own = state->partyUnits_;
        own.SetSize(heroes.size, -1);
        for (int i = 0; i < heroes.size; ++i) {
            ScriptObjectRef copy;
            ScriptObjectRef* ref = heroes.CopyAt(&copy, i);
            ScriptObjectRef* slot = &state->partyUnits_.data[i];
            slot->Reset();
            slot->Construct(*ref);
            copy.Reset();
        }
    }
    heroes.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
    if (heroes.data) {
        ScriptObjectRef* ref = heroes.data;
        for (int n = heroes.size; n != 0; --n) (ref++)->DestroyElement(0);
        gamecrt::operator_delete(heroes.data);
    }

    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayer(player)) DropPlayer(this, player);

    ++updateCount_;
    return 1;
}

EI_REWRITTEN(0x0058DFF0, CWorldServer::Update);
