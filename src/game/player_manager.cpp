// The world's player manager.
#include "player_manager.h"

#include "game_singletons.h"
#include "player_messages.h"
#include "world.h"

#include "ei/rebirth.h"

#include <new>

namespace {

constexpr int kPlayerType = 0x300A;      // a CPlayer

// Walks every player of `manager`, as the original's iterator does (the manager counted
// while it walks).
template <typename F> void ForEachPlayer(PlayerManager* manager, F f) {
    PlayerIterator it;
    for (CPlayer* player = it.First(manager); player; player = it.Next()) f(player);
}

// Rebirth's PlayerNumbers (addon's 10021B4A in place of the call of CPlayer::SetPlayerId):
// the player gets the highest number of the server world's other players + 1 (signed), or `id` when
// there is no player list, no other player or none numbered 0 or more. Written straight into +0x24.
int NextPlayerNumber(CPlayer* self, int id) {
    PlayerManager* players = g_serverWorld.PlayerList();
    if (!players) return id;
    int highest = -1;
    ForEachPlayer(players, [&](CPlayer* other) {
        if (other == self) return;
        const int number = other->playerId;
        if (number > highest) highest = number;
    });
    return highest >= 0 ? highest + 1 : id;
}

} // namespace

EngineObject* PlayerManager::NewObject(int playerType) { return NewPlayer(playerType); }

EngineObject* PlayerManager::NewPlayer(int playerType) {
    CPlayer* player = new CPlayer();
    player->type = playerType;
    return player;
}

CPlayer* PlayerManager::CreateSessionPlayer() {
    auto* player = static_cast<CPlayer*>(BaseManager::CreateObject(kPlayerType, -1));
    if (!player) return nullptr;
    BaseManager* specific = ContextAsWorld()->SpecificManager();
    player->SetConnection(specific->CreateObjectAs<PlayerConnection>(SideObjectType::Player, -1));
    player->AddRef();
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PlayerNumbers))
        player->playerId = NextPlayerNumber(player, 0);
    else
        player->SetPlayerId(0);
    return player;
}

void PlayerManager::ReleasePlayer(EngineObject* player) { player->Release(); }

void PlayerManager::ChangeId(EngineObject* object, int newId) {
    if (auto old = objects.find(static_cast<unsigned>(object->id)); old != objects.end()) objects.erase(old);
    object->id = newId;
    if (newId > maxId) maxId = newId;
    objects[static_cast<unsigned>(newId)] = object;
}

void PlayerManager::SendValueToAll(int value) {
    ForEachPlayer(this, [&](CPlayer* player) { player->Server()->SendValue12(value); });
}

void PlayerManager::SendTextToAll(const char* text) {
    ForEachPlayer(this, [&](CPlayer* player) {
        player->Server()->SendText13(text);
    });
}
