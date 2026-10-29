// WorldServer::Update: one world update. See world.h and docs/notes/world.md.
#include "world.h"
#include "console.h"

#include "combat.h"
#include "net_classes.h"
#include "net_link.h"
#include "player_manager.h"
#include "player_messages.h"
#include "script.h"
#include "unit.h"
#include "world_data.h"

#include "lib/str.h"
#include <cstring>
#include <memory>

namespace {

// -1 while no Possession spell has taken a unit.
bool NoPendingPossession(const PlayerServer* state) { return static_cast<int>(state->possessedId) < 0; }

// ---- Helpers ----------------------------------------------------------------------------------

// Disconnects a player (its connection and units).
// A player that lost its connection leaves the game.
void DropPlayer(WorldServer* world, CPlayer* player) {
    player->Disconnect();
    world->PlayerList()->ReleasePlayer(player);
    console::EndLine(console::g_netLogOut << worlddata::DisconnectingPlayerText() << player->playerId);
}

// Every unit of a connected player wakes the units within its hearing range (as SyncPlayer computes it, with
// another margin: the server half's movement factor 3 and statistics B0 values 0 and 2).
void WakeAroundUnits(WorldServer* world, CPlayer* player) {
    auto& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        CUnit* unit = node->second;
        auto* controller = unit->ServerController();
        float perception = static_cast<float>(controller->moveFactors[3] * controller->stats.perception[0].current);
        float minimum = controller->stats.perception[2].current;
        float range = perception >= minimum ? perception : minimum;   // the branch-free select
        float radius = static_cast<float>(static_cast<double>(range) * worlddata::kHearingScale + worlddata::kWakeMargin);
        world->WakeUnitsAround(unit->localPosition, radius);
    }
}

} // namespace

void WorldServer::Update() {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        netlink::NetLink* link = player->link;
        if (link && link->Connected() && player->connection->Receive() != 0)
            WakeAroundUnits(this, player);
        else
            DropPlayer(this, player);
    }

    if (WorldHandle* handle = worldHandle.Get()) {
        handle->Pump();
        AcceptNewPlayers(handle);
    }
    if (netlink::DriverSession* session = netSession.Get()) {
        net::ServerSession* server = net::g_gameServer;
        if (server && !(server->flags7 & RefCounted::kDetached)) session->PumpDrivers(server);
        AcceptNewPlayers(session);
    }

    if (!paused) {
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
            player->Tick();
        Tick(static_cast<char>(0));
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        player->Server()->UpdatePartyUnits();

    // The local players' heroes: the global "Heroes" group plus each one's own heroes, then given
    // back to every local player.
    ScriptGroup heroes;
    ScriptVars& globals = MainScript()->globals;
    auto& names = globals.groups.names;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (str::Compare(names[i], worlddata::HeroesGroupText()) == 0) {
            heroes = globals.groups.values[i];
            break;
        }
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        ScriptGroup sum;
        sum.Union(state->partyUnits, heroes);
        heroes = std::move(sum);
    }
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        if (!NoPendingPossession(state)) continue;
        state->partyUnits = heroes;
    }

    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next())
        if (!SyncPlayer(player)) DropPlayer(this, player);

    ++updateCount;
}
