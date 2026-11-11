// The local player's queries for the game interface: the game time, the local player's party, zone and variables,
// player 0's variables. See world.h.
#include "game/world/world.h"

#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/players/player_client.h"

#include "game/app/singletons.h"
#include "engine/core/named_floats.h"
#include "game/players/player.h"

#include <memory>
#include <new>

namespace {

// The local player record through the client's local view (no null checks).
Player* LocalPlayer() { return g_clientWorld.LocalView()->player; }

// A player's named float variable (its party's variables).
float Variable(Player* player, std::string_view name) { return player->GetParty()->variables.Get(name); }

} // namespace

bool IsMultiplayerClient() {
    net::ClientSession* client = net::g_gameClient;
    if (IsLive(client)) return true;
    return false;
}

float GameTime() {
    WorldClient* client = &g_clientWorld;
    return static_cast<float>(client->tick) + client->tickFraction;
}

Party* LocalPlayerData() { return LocalPlayer()->GetParty(); }

std::string LocalPlayerZone() { return LocalPlayer()->GetParty()->zone; }

float LocalPlayerVariable(std::string_view name) { return Variable(LocalPlayer(), name); }

float PlayerZeroVariable(std::string_view name) {
    if (IsMultiplayerClient()) return Variable(LocalPlayer(), name);
    Player* player = g_serverWorld.GetPlayer(0);
    if (!player) return 0.0f;
    return Variable(player, name);
}

NamedFloatTable PlayerZeroVariables(std::string_view prefix) {
    NamedFloatTable table;
    Player* player = IsMultiplayerClient() ? LocalPlayer() : g_serverWorld.GetPlayer(0);
    if (player) player->GetParty()->variables.CopyPrefixed(prefix, &table);
    return table;
}
