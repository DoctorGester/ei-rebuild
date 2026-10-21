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

__declspec(naked) int __fastcall SyncPlayerAt(CWorldServer*, void*, CPlayer*) EI_ORIGINAL(0x0058B750)
// 00665A10. Disconnects a player (its connection and units).
// Script groups: constructor 004C2030 (base vtable), destructor 004C2290, assignment 004C1290,
// union 004B2620; the global group / name / element getters by value (004C3B00, 004C2680,
// 004C21E0); 004CC7D0 (__stdcall) releases an element range; 004CE1E0 (RET 4) one element's
// destructor; 004C2050 SetSize.
void __stdcall RefsRelease(ScriptObjectRef* a0, int a1) { gamearrays::ObjectRefs::DestructElements(a0, a1); }

// A player that lost its connection leaves the game.
void DropPlayer(CWorldServer* world, CPlayer* player) {
    PlayerDisconnectOriginal(player);
    static_cast<CPlayerManager*>(world->PlayerList())->ReleasePlayer(player);
    NetworkLog()->PutText(worlddata::DisconnectingPlayerText())->PutInt(player->playerId_)->PutChar('\n')
        ->Apply(libcrt::FlushManipulator);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer
// computes it, with another margin: the server half's movement factor 3 and statistics B0
// values 0 and 2).
void WakeAroundUnits(CWorldServer* world, CPlayer* player) {
    auto& units = player->units_;
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CUnit* unit = node->value;
        auto* controller = unit->ServerController();
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
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        netlink::NetLink* link = player->link_;
        if (link && link->Connected() && player->connection_->Receive() != 0)
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
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            PlayerTickOriginal(player);
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
            alignas(4) unsigned char storage[sizeof(mfc::CString)];
            mfc::CString* name = reinterpret_cast<mfc::CString*>(mfc::impl::StringArrayAbi::GetAt(
        reinterpret_cast<const mfc::impl::StringArrayAbi::Array*>(&names), nullptr, reinterpret_cast<mfc::impl::CString*>(storage), i));
            found = gamecrt::mbscmp(static_cast<const char*>(*name),
                                    worlddata::HeroesGroupText()) == 0;
            name->~CString();
        }
        if (found) {
            alignas(4) std::uint8_t storage[sizeof(ScriptGroup)];
            ScriptGroup* group = globals.groups.CopyAt(reinterpret_cast<ScriptGroup*>(storage), i);
            heroes.CopyFrom(*group);
            ScriptGroupDestructOriginal((void*)reinterpret_cast<ScriptGroup*>(storage));
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
        sum.vtable = scriptvtable::kRefsBase;
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
    heroes.vtable = scriptvtable::kRefsBase;
    if (heroes.data) {
        ScriptObjectRef* ref = heroes.data;
        for (int n = heroes.size; n != 0; --n) (ref++)->DestroyElement(0);
        gamecrt::operator_delete(heroes.data);
    }

    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayerAt(this, nullptr, player)) DropPlayer(this, player);

    ++updateCount_;
    return 1;
}

EI_REPLACE_METHOD(0x0058DFF0, CWorldServer::Update);
