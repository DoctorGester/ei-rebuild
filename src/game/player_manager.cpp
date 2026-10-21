// The world's player manager (player_manager.h; A-H15).
#include "player_manager.h"

#include "originals_gameplay.h"
#include "player_messages.h"
#include "world.h"
#include "game_singletons.h"

#include "../lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"

#include <new>

namespace {

constexpr int kPlayerType = 0x300A;      // a CPlayer
constexpr int kConnectionType = 0x49;    // the side's player connection (CPlayerServer / CPlayerClient)

// The player's connection: the server side of it (only the server's manager sends to all).
CPlayerServer* Server(CEngineObject* player) {
    return static_cast<CPlayer*>(player)->Server();
}

// Walks every player of `manager`, as the original's inlined iterator does (the manager counted
// while it walks).
template <typename F> void ForEachPlayer(CBaseManager* manager, F f) {
    ObjectListIterator it{nullptr, nullptr};
    for (CEngineObject* player = it.First(manager); player; player = it.Next()) f(player);
    if (it.manager) --it.manager->refCount_;
}

} // namespace

BOOL CPlayerManager::CreatePlayers() { return CBaseManager::Create() != 0 ? 1 : 0; }
int CPlayerManager::Create() { return CreatePlayers(); }
CEngineObject* CPlayerManager::NewObject(int type) { return NewPlayer(type); }

CEngineObject* CPlayerManager::NewPlayer(int type) {
    void* memory = gamecrt::operator_new(sizeof(CPlayer));
    if (!memory) return nullptr;
    CPlayer* player = static_cast<CPlayer*>(memory)->Construct();
    if (!player) return nullptr;
    player->type_ = type;
    return player;
}

namespace {
// Rebirth's PlayerNumbers (addon's 10021B4A in place of the call of CPlayer::SetPlayerId at 0066C1C2):
// the player gets the highest number of the server world's other players + 1 (signed), or `id` when
// there is no player list, no other player or none numbered 0 or more. Written straight into +0x24.
int NextPlayerNumber(CPlayer* self, int id) {
    CBaseManager* players = TheServerWorld()->PlayerList();
    if (!players) return id;
    int highest = -1;
    ForEachPlayer(players, [&](CEngineObject* other) {
        if (other == self) return;
        const int number = static_cast<CPlayer*>(other)->playerId_;
        if (number > highest) highest = number;
    });
    return highest >= 0 ? highest + 1 : id;
}
} // namespace

CEngineObject* CPlayerManager::CreateSessionPlayer() {
    auto* player = static_cast<CPlayer*>(CBaseManager::CreateObjectImpl(kPlayerType, -1));
    if (!player) return nullptr;
    CBaseManager* specific = static_cast<CWorld*>(context_)->SpecificManager();
    player->SetConnection(static_cast<CPlayerConnection*>(specific->CreateObject(kConnectionType, -1)));
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
    ForEachPlayer(this, [&](CEngineObject* player) { Server(player)->SendValue12(unused, value); });
}

void CPlayerManager::SendTextToAll(int unused, const char* text) {
    ForEachPlayer(this, [&](CEngineObject* player) {
        Server(player)->SendText13(static_cast<std::uint32_t>(unused), text);
    });
}

EI_REWRITTEN(0x0066C0F0, CPlayerManager::CreatePlayers);
EI_REWRITTEN(0x0066C110, CPlayerManager::NewPlayer);
EI_REPLACE_METHOD(0x0066C180, CPlayerManager::CreateSessionPlayer);
EI_REPLACE_METHOD(0x0066C1D0, CPlayerManager::ReleasePlayer);
EI_REWRITTEN(0x0066C1E0, CPlayerManager::ChangeId);
EI_REPLACE_METHOD(0x0066C340, CPlayerManager::SendValueToAll);
EI_REPLACE_METHOD(0x0066C3C0, CPlayerManager::SendTextToAll);
