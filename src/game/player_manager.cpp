// The world's player manager.
#include "player_manager.h"

#include "player_messages.h"
#include "world.h"
#include "game_singletons.h"

#include "../lib/crt.h"

#include "ei/rebirth.h"

#include <new>

namespace {

constexpr int kPlayerType = 0x300A;      // a CPlayer
constexpr int kConnectionType = 0x49;    // the side's player connection (CPlayerServer / CPlayerClient)

// Walks every player of `manager`, as the original's inlined iterator does (the manager counted
// while it walks).
template <typename F> void ForEachPlayer(CPlayerManager* manager, F f) {
    PlayerIterator it;
    for (CPlayer* player = it.First(manager); player; player = it.Next()) f(player);
}

} // namespace

BOOL CPlayerManager::CreatePlayers() { return CBaseManager::Create() != 0 ? 1 : 0; }
int CPlayerManager::Create() { return CreatePlayers(); }
CEngineObject* CPlayerManager::NewObject(int type) { return NewPlayer(type); }

CEngineObject* CPlayerManager::NewPlayer(int type) {
    CPlayer* player = new CPlayer();
    if (!player) return nullptr;
    player->type_ = type;
    return player;
}

namespace {
// Rebirth's PlayerNumbers (addon's 10021B4A in place of the call of CPlayer::SetPlayerId):
// the player gets the highest number of the server world's other players + 1 (signed), or `id` when
// there is no player list, no other player or none numbered 0 or more. Written straight into +0x24.
int NextPlayerNumber(CPlayer* self, int id) {
    CPlayerManager* players = TheServerWorld()->PlayerList();
    if (!players) return id;
    int highest = -1;
    ForEachPlayer(players, [&](CPlayer* other) {
        if (other == self) return;
        const int number = other->playerId_;
        if (number > highest) highest = number;
    });
    return highest >= 0 ? highest + 1 : id;
}
} // namespace

CPlayer* CPlayerManager::CreateSessionPlayer() {
    auto* player = static_cast<CPlayer*>(CBaseManager::CreateObject(kPlayerType, -1));
    if (!player) return nullptr;
    CBaseManager* specific = ContextAsWorld()->SpecificManager();
    player->SetConnection(specific->CreateObjectAs<CPlayerConnection>(kConnectionType, -1));
    ++player->refCount_;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::PlayerNumbers))
        player->playerId_ = NextPlayerNumber(player, 0);
    else
        player->SetPlayerId(0);
    return player;
}

void CPlayerManager::ReleasePlayer(CEngineObject* player) { player->Release(); }

void CPlayerManager::ChangeId(CEngineObject* object, int id) {
    if (ObjectMap::Node* old = objects_.Find(static_cast<unsigned>(object->id_))) objects_.Erase(old);
    object->id_ = id;
    if (id > maxId_) maxId_ = id;
    objects_[static_cast<unsigned>(id)] = object;
}

void CPlayerManager::SendValueToAll(int unused, int value) {
    ForEachPlayer(this, [&](CPlayer* player) { player->Server()->SendValue12(unused, value); });
}

void CPlayerManager::SendTextToAll(int unused, const char* text) {
    ForEachPlayer(this, [&](CPlayer* player) {
        player->Server()->SendText13(static_cast<std::uint32_t>(unused), text);
    });
}
